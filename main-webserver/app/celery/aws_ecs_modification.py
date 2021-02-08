import math
import logging

from celery import shared_task
from flask import current_app

from app.helpers.utils.aws.base_ecs_client import ECSClient
from app.helpers.utils.aws.aws_resource_locks import (
    lock_container_and_update,
    spin_lock,
)
from app.helpers.utils.general.logs import fractal_log
from app.models import db, ClusterInfo, RegionToAmi, UserContainer
from app.helpers.utils.general.sql_commands import fractal_sql_commit


@shared_task(bind=True)
def update_cluster(self, region_name="us-east-1", cluster_name=None, ami=None):
    """
    Updates a specific cluster to use a new AMI
    :param self (CeleryInstance): the celery instance running the task
    :param region_name (str): which region the cluster is in
    :param cluster_name (str): which cluster to update
    :param ami (str): which AMI to use
    :return: which cluster was updated
    """
    all_regions = RegionToAmi.query.all()
    region_to_ami = {region.region_name: region.ami_id for region in all_regions}
    if ami is None:
        ami = region_to_ami[region_name]

    self.update_state(
        state="PENDING",
        meta={
            "msg": (f"updating cluster {cluster_name} on ECS to ami {ami} in region {region_name}"),
        },
    )
    fractal_log(
        function="update_cluster",
        label="None",
        logs=f"updating cluster {cluster_name} on ECS to ami {ami} in region {region_name}",
    )

    all_regions = RegionToAmi.query.all()
    region_to_ami = {region.region_name: region.ami_id for region in all_regions}
    if ami is None:
        ami = region_to_ami[region_name]

    # first, delete every unassigned container
    unassigned_containers = UserContainer.query.filter_by(
        cluster=cluster_name, is_assigned=False
    ).all()
    for container in unassigned_containers:
        container_name = container.container_id

        # First, we lock the container in the DB to make sure it doesn't get assigned while being deleted.
        if spin_lock(container_name) < 0:
            fractal_log(
                function="update_cluster",
                label=container_name,
                logs="spin_lock took too long.",
                level=logging.ERROR,
            )

            raise Exception("Failed to acquire resource lock.")

        fractal_log(
            function="update_cluster",
            label=str(container_name),
            logs="Beginning to delete unassigned container {container_name}. Goodbye!".format(
                container_name=container_name
            ),
        )

        lock_container_and_update(
            container_name=container_name, state="DELETING", lock=True, temporary_lock=10
        )

        # then, we delete it

        container_cluster = container.cluster
        container_location = container.location
        ecs_client = ECSClient(
            base_cluster=container_cluster, region_name=container_location, grab_logs=False
        )

        ecs_client.add_task(container_name)
        if not ecs_client.check_if_done(offset=0):
            ecs_client.stop_task(reason="API triggered task stoppage", offset=0)
            self.update_state(
                state="PENDING",
                meta={
                    "msg": "Container {container_name} begun stoppage".format(
                        container_name=container_name,
                    )
                },
            )
        fractal_sql_commit(db, lambda db, x: db.session.delete(x), container)

    ecs_client = ECSClient(launch_type="EC2", region_name=region_name)
    ecs_client.update_cluster_with_new_ami(cluster_name, ami)

    self.update_state(
        state="SUCCESS",
        meta={
            "msg": f"updated cluster {cluster_name} on ECS to ami {ami} in region {region_name}",
        },
    )


@shared_task(bind=True)
def update_region(self, region_name="us-east-1", ami=None):
    """
    Updates all clusters in a region to use a new AMI
    calls update_cluster under the hood
    :param self (CeleryInstance): the celery instance running the task
    :param region_name (str): which region the cluster is in
    :param ami (str): which AMI to use
    :return: which cluster was updated
    """
    region_to_ami = RegionToAmi.query.filter_by(
        region_name=region_name,
    ).first()
    if region_to_ami is None:
        raise ValueError(f"Region {region_name} is not in db.")

    if ami is None:
        # use existing AMI
        ami = region_to_ami.ami_id
    else:
        # # update db with new AMI
        region_to_ami.ami_id = ami
        fractal_sql_commit(db)
        fractal_log(
            "update_region",
            None,
            f"updated AMI in {region_name} to {ami}",
        )

    self.update_state(
        state="PENDING",
        meta={
            "msg": (f"updating to ami {ami} in region {region_name}"),
        },
    )

    all_clusters = list(ClusterInfo.query.filter_by(location=region_name).all())
    all_clusters = [cluster for cluster in all_clusters if "cluster" in cluster.cluster]

    if len(all_clusters) == 0:
        fractal_log(
            function="update_region",
            label=None,
            logs=f"No clusters found in region {region_name}",
            level=logging.WARNING,
        )
        self.update_state(
            state="SUCCESS",
            meta={"msg": f"No clusters in region {region_name}", "tasks": ""},
        )
        return

    fractal_log(
        function="update_region",
        label=None,
        logs=f"Updating clusters: {all_clusters}",
    )
    tasks = []
    for cluster in all_clusters:
        task = update_cluster.delay(region_name, cluster.cluster, ami)
        tasks.append(task.id)

    fractal_log(
        function="update_region",
        label=None,
        logs="update_region is returning with success. It spun up the "
        f"following update_cluster tasks: {tasks}",
    )

    # format tasks as space separated strings (used by AMI creation workflow to poll for success)
    delim = " "
    formatted_tasks = delim.join(tasks)

    self.update_state(
        state="SUCCESS",
        meta={"msg": f"updated to ami {ami} in region {region_name}", "tasks": formatted_tasks},
    )


@shared_task(bind=True)
def manual_scale_cluster(self, cluster: str, region_name: str):
    """
    Manually scales the cluster according the the following logic:
        1. see if active_tasks + pending_tasks > num_instances * AWS_TASKS_PER_INSTANCE. if so,
            we should trigger a scale down.
        2. check if there are instances with 0 tasks. sometimes AWS can suboptimally distribute
            our workloads, so we want to make sure there are instances we can actually delete.
            If AWS does poorly distribute our loads, we log it.
        3. trigger a scale down so only instances with tasks remain.
    This function does not handle outscaling at the moment.
    This function can be expanded to handle more custom scaling logic as we grow.
    Args:
        cluster: cluster to manually scale
        region_name: region that cluster resides in
    """
    self.update_state(
        state="PENDING",
        meta={
            "msg": f"Checking if cluster {cluster} should be scaled.",
        },
    )

    factor = int(current_app.config["AWS_TASKS_PER_INSTANCE"])

    ecs_client = ECSClient(launch_type="EC2", region_name=region_name)
    cluster_data = ecs_client.describe_cluster(cluster)
    keys = [
        "runningTasksCount",
        "pendingTasksCount",
        "registeredContainerInstancesCount",
    ]
    for k in keys:
        if k not in cluster_data:
            raise ValueError(
                f"Expected key {k} in AWS describle_cluster API response. Got: {cluster_data}"
            )

    num_tasks = cluster_data["runningTasksCount"] + cluster_data["pendingTasksCount"]
    num_instances = cluster_data["registeredContainerInstancesCount"]
    expected_num_instances = math.ceil(num_tasks / factor)

    if expected_num_instances == num_instances:
        self.update_state(
            state="SUCCESS",
            meta={
                "msg": f"Cluster {cluster} did not need any scaling.",
            },
        )
        return

    # now we check if there are actually empty instances
    instances = ecs_client.list_container_instances(cluster)
    instances_tasks = ecs_client.describe_container_instances(cluster, instances)

    empty_instances = 0
    for instance_task_data in instances_tasks:
        if "runningTasksCount" not in instance_task_data:
            msg = (
                "Expected key runningTasksCount in AWS describe_container_instances response."
                f" Got: {instance_task_data}"
            )
            raise ValueError(msg)
        rtc = instance_task_data["runningTasksCount"]
        if rtc == 0:
            empty_instances += 1

    if empty_instances == 0:
        msg = (
            f"Cluster {cluster} had {num_instances} instances but should have"
            f" {expected_num_instances}. Number of total tasks: {num_tasks}. However, no instance"
            " is empty so a scale down cannot be triggered. This means AWS ECS has suboptimally"
            " distributed tasks onto instances."
        )
        fractal_log(
            "manual_scale_cluster",
            None,
            msg,
            level=logging.INFO,
        )

        self.update_state(
            state="SUCCESS",
            meta={
                "msg": "Could not scale-down because no empty instances.",
            },
        )

    else:
        # at this point, we have detected scale-down should happen and know that
        # there are some empty instances for us to delete. we can safely do a scale-down.
        asg_list = ecs_client.get_auto_scaling_groups_in_cluster(cluster)
        if len(asg_list) != 1:
            raise ValueError(f"Expected 1 ASG but got {len(asg_list)} for cluster {cluster}")

        asg = asg_list[0]
        # keep whichever instances are not empty
        desired_capacity = num_instances - empty_instances
        ecs_client.set_auto_scaling_group_capacity(asg, desired_capacity)

        self.update_state(
            state="SUCCESS",
            meta={
                "msg": f"Scaled cluster {cluster} from {num_instances} to {desired_capacity}.",
            },
        )
