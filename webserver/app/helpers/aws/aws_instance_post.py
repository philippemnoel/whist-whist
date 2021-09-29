import threading
import time

from collections import defaultdict
from sys import maxsize
from typing import Any, DefaultDict, List, Optional
import requests
from flask import current_app
from app.database.models.cloud import (
    db,
    RegionToAmi,
    InstanceInfo,
    MandelboxInfo,
    InstancesWithRoomForMandelboxes,
    LingeringInstances,
)
from app.utils.db.db_utils import set_local_lock_timeout
from app.utils.aws.base_ec2_client import EC2Client
from app.utils.general.name_generation import generate_name
from app.utils.general.logs import fractal_logger
from app.constants.mandelbox_host_states import MandelboxHostState
from app.constants.ec2_instance_states import EC2InstanceState

bundled_region = {
    "us-east-1": ["us-east-2"],
    "us-east-2": ["us-east-1"],
    "us-west-1": ["us-west-2"],
    "us-west-2": ["us-west-1"],
    "ca-central-1": ["us-east-1"],
}

# The number of GPUs on each instance type
instance_type_to_gpu_map = {
    "g4dn.xlarge": 1,
    "g4dn.2xlarge": 1,
    "g4dn.4xlarge": 1,
    "g4dn.8xlarge": 1,
    "g4dn.16xlarge": 1,
    "g4dn.12xlarge": 4,
}

# The number of vCPUs on each instance type
instance_type_to_vcpu_map = {
    "g4dn.xlarge": 4,
    "g4dn.2xlarge": 8,
    "g4dn.4xlarge": 16,
    "g4dn.8xlarge": 32,
    "g4dn.16xlarge": 64,
    "g4dn.12xlarge": 48,
}

# Currently the limiting factors are GPU count and
# vCPU count. Our two constraints are mandelboxes/GPU
# (configured to 2 in the host service) and vCPUs/mandelbox
# (which we set to 4 here). This determines a very simple
# allocation setup that we can flesh out later.

HOST_SERVICE_MANDELBOXES_PER_GPU = 2
VCPUS_PER_MANDELBOX = 4

type_to_number_map = {
    k: min(
        instance_type_to_gpu_map[k] * HOST_SERVICE_MANDELBOXES_PER_GPU,
        instance_type_to_vcpu_map[k] // VCPUS_PER_MANDELBOX,
    )
    for k in instance_type_to_gpu_map.keys() & instance_type_to_vcpu_map.keys()
}


def get_base_free_mandelboxes(instance_type: str) -> int:
    """
    Returns the number of containers an instance of this type can hold
    Args:
        instance_type: Which type of instance this is

    Returns:
        How many containers can it hold
    """
    return type_to_number_map[instance_type]


def get_instance_id(instance: InstanceInfo) -> str:
    """
    Get the ID for an instance in the DB, using the AWS client
    Args:
        which instance whose ID to get
    Returns:
        the EC2 ID of that instance
    """
    return str(instance.cloud_provider_id.strip("aws-"))


def check_instance_exists(instance_id: str, location: str) -> bool:
    """
    Checks whether a specified instance actually exists and is not
    stopped/terminated, using the AWS client
    Args:
        instance_id: the id of the instance to query
        location: the region to check in
    Returns:
        True if the instance actually exists and is not stopped/terminated, else False
    """
    ec2_client = EC2Client(region_name=location)
    status = ec2_client.get_instance_states([instance_id])[0]
    return status not in (
        EC2InstanceState.DOES_NOT_EXIST,
        EC2InstanceState.STOPPED,
        EC2InstanceState.TERMINATED,
    )


def terminate_instance(instance: InstanceInfo) -> None:
    """
    Terminates a given instance using the AWS client
    Args:
        instance: which instance to terminate

    Returns: None

    """
    instance_id = get_instance_id(instance)
    if check_instance_exists(instance_id, instance.location):
        ec2_client = EC2Client(region_name=instance.location)
        ec2_client.stop_instances([instance_id])
    db.session.delete(instance)
    db.session.commit()


def find_enabled_regions() -> Any:
    """
    Returns a list of regions that are currently active
    """

    return RegionToAmi.query.filter_by(ami_active=True).distinct(RegionToAmi.region_name)


def find_instance(region: str, client_commit_hash: str) -> Optional[str]:
    """
    Given a list of regions, finds (if it can) an instance in that region
    or a neighboring region with space. If it succeeds, returns the instance name.
    Else, returns None
    Args:
        regions: a list of regions sorted by proximity

    Returns: either an instance name or None

    """
    bundled_regions = bundled_region.get(region, [])
    # InstancesWithRoomForMandelboxes is sorted in DESC
    # with number of mandelboxes running, So doing a
    # query with limit of 1 returns the instance with max
    # occupancy which can improve resource utilization.
    instance_with_max_mandelboxes: Optional[InstancesWithRoomForMandelboxes] = (
        InstancesWithRoomForMandelboxes.query.filter_by(
            commit_hash=client_commit_hash,
            location=region,
            status=MandelboxHostState.ACTIVE,
        )
        .limit(1)
        .one_or_none()
    )

    if instance_with_max_mandelboxes is None:
        # If we are unable to find the instance in the required region,
        # let's try to find an instance in nearby AZ
        # that doesn't impact the user experience too much.
        instance_with_max_mandelboxes = (
            InstancesWithRoomForMandelboxes.query.filter(
                InstancesWithRoomForMandelboxes.location.in_(bundled_regions)
            )
            .filter_by(
                commit_hash=client_commit_hash,
                status=MandelboxHostState.ACTIVE,
            )
            .limit(1)
            .one_or_none()
        )
    if instance_with_max_mandelboxes is None:
        return None
    else:
        # 5sec arbitrarily decided as sufficient timeout when using with_for_update
        set_local_lock_timeout(5)
        # We are locking InstanceInfo row to ensure that we are not assigning a user/mandelbox
        # to an instance that might be marked as DRAINING. With the locking, the instance will be
        # marked as DRAINING after the assignment is complete but not during the assignment.
        avail_instance: Optional[InstanceInfo] = (
            InstanceInfo.query.filter_by(instance_name=instance_with_max_mandelboxes.instance_name)
            .with_for_update()
            .one_or_none()
        )
        # The instance that was available earlier might be lost before we try to grab a lock.
        if avail_instance is None or avail_instance.status != MandelboxHostState.ACTIVE:
            return None
        else:
            return str(avail_instance.instance_name)


def _get_num_new_instances(region: str, ami_id: str) -> int:
    """
    This function, given a region and an AMI ID, returns the number of new instances
    that we want to have spun up in that region with that AMI.  Returns
     -sys.maxsize for cases of "that AMI is inactive and we want to get rid of
     all instances running it when possible", since in that case
     we want to remove every instance of that type (and -sys.maxsize functions as
     negative infinity).

     At the moment, our scaling algorithm is
     - 'if we have less than 20 mandelboxes in a valid AMI/region pair,
     make a new instance'
     - 'Else, if we have a full instance of extra space more than 20, try to
     stop an instance'
     - 'Else, we're fine'.
    Args:
        region: which region we care about
        ami_id: which AMI ID we're checking

    Returns: the integer number of new instances we want live

    """

    # If the region is invalid or the AMI is not current, we want no buffer
    if region not in {x.region_name for x in RegionToAmi.query.all()}:
        return -maxsize
    active_ami_for_given_region = RegionToAmi.query.filter_by(
        region_name=region, ami_active=True
    ).one_or_none()
    if active_ami_for_given_region is None:
        return -maxsize
    if ami_id != active_ami_for_given_region.ami_id:
        return -maxsize
    # Now, we want to get the average number of mandelboxes per instance in that region
    # and the number of free mandelboxes
    all_instances = list(InstanceInfo.query.filter_by(location=region, aws_ami_id=ami_id).all())

    if len(all_instances) == 0:
        # If there are no instances running, we want one.
        return 1
    all_free_instances = list(
        InstancesWithRoomForMandelboxes.query.filter_by(location=region, aws_ami_id=ami_id).all()
    )
    num_free_mandelboxes = sum(
        instance.mandelbox_capacity - instance.num_running_mandelboxes
        for instance in all_free_instances
    )
    avg_mandelbox_capacity = sum(instance.mandelbox_capacity for instance in all_instances) / len(
        all_instances
    )

    # And then figure out how many instances we need to spin up/purge to get 20 free total
    # Check config DB for the correct value for the environment.
    desired_free_mandelboxes = int(current_app.config["DESIRED_FREE_MANDELBOXES"])

    if num_free_mandelboxes < desired_free_mandelboxes:
        return int(current_app.config["DEFAULT_INSTANCE_BUFFER"])

    if num_free_mandelboxes >= (desired_free_mandelboxes + avg_mandelbox_capacity):
        return -1

    return 0


# We make a collection of mutexes, one for each region and AMI.
# Defaultdict is a lazy dict, that creates default objects (in this case locks)
# as needed.
# Note that this only works if we have 1 web process,
# Multiprocess support will require DB synchronization/locking
scale_mutex: DefaultDict[str, threading.Lock] = defaultdict(threading.Lock)


def do_scale_up_if_necessary(
    region: str, ami: str, force_buffer: int = 0, **kwargs: Any
) -> List[str]:
    """
    Scales up new instances as needed, given a region and AMI to check
    Specifically, if we want to add X instances (_get_num_new_instances
    returns X), we generate X new names and spin up new instances with those
    names.
    Args:
        region: which region to check for scaling
        ami: which AMI to scale with
        force_buffer: this will be used to override the recommendation for
        number of instances to be launched by `_get_num_new_instances`

    Returns: List of names of the instances created, if any.

    """
    new_instance_names = []
    with scale_mutex[f"{region}-{ami}"], kwargs["flask_app"].app_context():

        # num_new indicates how many new instances need to be spun up from
        # the ami that is passed in. Usually, we calculate that through
        # invoking `_get_num_new_instances` function. However, we can chose to
        # ignore that recommendation by passing in the `force_buffer` value.
        num_new = 0
        if force_buffer is not None and force_buffer > 0:
            num_new = force_buffer
        else:
            num_new = _get_num_new_instances(region, ami)

        ami_obj = RegionToAmi.query.filter_by(region_name=region, ami_id=ami).one_or_none()

        if num_new > 0:
            client = EC2Client(region_name=region)
            base_name = generate_name(starter_name=f"ec2-{region}")
            # TODO: test that the simple num_cpu/2 heuristic is accurate
            base_number_free_mandelboxes = get_base_free_mandelboxes(
                current_app.config["AWS_INSTANCE_TYPE_TO_LAUNCH"]
            )
            for index in range(num_new):
                instance_ids = client.start_instances(
                    image_id=ami,
                    instance_name=base_name + f"-{index}",
                    num_instances=1,
                    instance_type=current_app.config["AWS_INSTANCE_TYPE_TO_LAUNCH"],
                )
                # Setting last update time to -1 indicates that the instance
                # hasn't told the webserver it's live yet. We add the rows to
                # the DB now so that future scaling operations don't
                # double-scale.
                new_instance = InstanceInfo(
                    location=region,
                    aws_ami_id=ami,
                    cloud_provider_id=f"aws-{instance_ids[0]}",
                    instance_name=base_name + f"-{index}",
                    aws_instance_type=current_app.config["AWS_INSTANCE_TYPE_TO_LAUNCH"],
                    mandelbox_capacity=base_number_free_mandelboxes,
                    last_updated_utc_unix_ms=-1,
                    creation_time_utc_unix_ms=int(time.time() * 1000),
                    status=MandelboxHostState.PRE_CONNECTION,
                    commit_hash=ami_obj.client_commit_hash,
                    ip="",  # Will be set by `host_service` once it boots up.
                )
                new_instance_names.append(new_instance.instance_name)
                db.session.add(new_instance)
                db.session.commit()
    return new_instance_names


def drain_instance(instance: InstanceInfo) -> bool:
    """
    Attempts to drain an instance, logging an error and marking unresponsive
    if it cannot be drained, and removing from the database if the instance
    in question does not actually exist. Note that we will terminate instances
    that do not have a valid IP (since host service is not up/connected yet).

    Note that we should not call this function multiple times on a single host
    since subsequent calls will fail and we will unfairly mark the host
    service as unresponsive.

    Args:
        instance: The instance to drain

    Returns:
        True if the instance is marked for draining, else False.

    """
    # We need to modify the status to DRAINING to ensure that we don't assign a new
    # mandelbox to the instance. We need to commit here as we don't want to enter a
    # deadlock with host service where it tries to modify the instance_info row.
    old_status = instance.status
    instance.status = MandelboxHostState.DRAINING
    db.session.commit()
    job_status = False
    if (
        old_status == MandelboxHostState.PRE_CONNECTION
        or instance.ip is None
        or str(instance.ip) == ""
        or not check_instance_exists(get_instance_id(instance), instance.location)
    ):
        terminate_instance(instance)
        job_status = True
    else:
        # If the instance exists, tell it to drain
        try:
            base_url = f"https://{instance.ip}:{current_app.config['HOST_SERVICE_PORT']}"
            resp = requests.post(
                f"{base_url}/drain_and_shutdown",
                json={"auth_secret": current_app.config["FRACTAL_ACCESS_TOKEN"]},
                verify=False,
            )
            resp.raise_for_status()
            job_status = True
        except requests.exceptions.RequestException as error:
            fractal_logger.error(
                "Unable to send drain_and_shutdown request to host service"
                f" on instance {instance.instance_name}: {error}"
            )
            instance.status = MandelboxHostState.HOST_SERVICE_UNRESPONSIVE
            db.session.commit()
    return job_status


def try_scale_down_if_necessary(region: str, ami: str) -> None:
    """
    Scales down new instances as needed, given a region and AMI to check
    Specifically, if we want to remove X instances (_get_num_new_instances
    returns -X), we get as many inactive instances as we can (up to X)
    and stop them.
    Args:
        region: which region to check for scaling
        ami: which AMI to scale with

    Returns: None

    """
    with scale_mutex[f"{region}-{ami}"]:
        num_new = _get_num_new_instances(region, ami)
        instances = InstanceInfo.query.filter_by(location=region, aws_ami_id=ami).all()
        fractal_logger.info(f"ami {region}/{ami} | instances {len(instances)} | delta {num_new}")
        for instance in instances:
            mandelboxes = MandelboxInfo.query.filter_by(instance_name=instance.instance_name).all()
            fractal_logger.info(
                f">>> instance {instance.instance_name} | mandelboxes {len(mandelboxes)} | status"
                f" {instance.status}"
            )
            for mandelbox in mandelboxes:
                fractal_logger.info(
                    f">>> >>> mandelbox {mandelbox.mandelbox_id} | user {mandelbox.user_id} |"
                    f" status {mandelbox.status}"
                )
        if num_new < 0:
            # we only want to scale down unused instances
            available_empty_instances = list(
                InstancesWithRoomForMandelboxes.query.filter_by(
                    location=region, aws_ami_id=ami, num_running_mandelboxes=0
                )
                .limit(abs(num_new))
                .all()
            )
            if len(available_empty_instances) == 0:
                return
            for instance in available_empty_instances:
                # grab a lock on the instance to ensure nothing new's being assigned to it
                instance_info = InstanceInfo.query.with_for_update().get(instance.instance_name)
                instance_mandelboxes = InstancesWithRoomForMandelboxes.query.filter_by(
                    instance_name=instance.instance_name
                ).one_or_none()
                if (
                    instance_mandelboxes is None
                    or instance_mandelboxes.num_running_mandelboxes != 0
                ):
                    db.session.commit()
                    continue
                fractal_logger.info(f">>> sending drain request to {instance.instance_name}")
                drain_instance(instance_info)


def try_scale_down_if_necessary_all_regions() -> None:
    """
    Runs try_scale_down_if_necessary on every region/AMI pair in our db

    """
    region_and_ami_list = [
        (region.location, region.aws_ami_id)
        for region in InstanceInfo.query.distinct(
            InstanceInfo.location, InstanceInfo.aws_ami_id
        ).all()
    ]
    for region, ami in region_and_ami_list:
        # grab a lock on this region/ami pair
        region_row = (
            RegionToAmi.query.filter_by(region_name=region, ami_id=ami)
            .with_for_update()
            .one_or_none()
        )
        if not region_row.protected_from_scale_down:
            try_scale_down_if_necessary(region, ami)
        # and release it after scaling
        db.session.commit()


def check_and_handle_lingering_instances() -> None:
    """
    Drains all lingering instances when called.
    Returns:
        None

    """
    # selects all lingering instances to drained except ones with status HOST_SERVICE_UNRESPONSIVE
    # instances with status HOST_SERVICE_UNRESPONSIVE + an associated mandelbox are left untouched
    # but instances with the status + are not associated with a mandelbox will be manually removed
    lingering_instances = [
        instance.instance_name
        for instance in LingeringInstances.query.filter(
            LingeringInstances.status != "HOST_SERVICE_UNRESPONSIVE"
        ).all()
    ]
    for instance_name in lingering_instances:
        set_local_lock_timeout(5)
        instance_info = InstanceInfo.query.with_for_update().get(instance_name)
        fractal_logger.info(f"Instance {instance_name} was lingering and is being drained")
        drain_instance(instance_info)
