import math

from typing import Optional

from celery import Task, shared_task
from flask import current_app

from app.helpers.utils.aws.base_ecs_client import ECSClient, FractalECSClusterNotFoundException
from app.helpers.utils.aws.aws_resource_integrity import ensure_container_exists
from app.helpers.utils.db.db_utils import set_local_lock_timeout
from app.helpers.utils.general.logs import fractal_logger
from app.models import (
    db,
    ClusterInfo,
    InstanceInfo,
    RegionToAmi,
    UserContainer,
    SupportedAppImages,
)
from app.helpers.utils.general.sql_commands import fractal_sql_commit
from app.celery.aws_celery_exceptions import InvalidArguments, InvalidAppId, InvalidCluster


@shared_task(bind=True)
def update_cluster(
    self: Task,
    region_name: Optional[str] = "us-east-1",
    cluster_name: Optional[str] = None,
    ami: Optional[str] = None,
) -> None:
    """
    Updates a specific cluster to use a new AMI

    Args:
        self (CeleryInstance): the celery instance running the task
        region_name (Optional[str]): which region the cluster is in
        cluster_name (Optional[str]): which cluster to update
        ami (Optional[str]): which AMI to use

    Returns:
        None, though cluster update info stored in state
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
    fractal_logger.info(
        f"updating cluster {cluster_name} on ECS to ami {ami} in region {region_name}"
    )

    # 30sec arbitrarily decided as sufficient timeout when using with_for_update
    set_local_lock_timeout(30)

    # We must delete every unassigned container in the cluster. Locks using with_for_update()
    unassigned_containers = (
        UserContainer.query.with_for_update().filter_by(cluster=cluster_name, user_id=None).all()
    )
    for container in unassigned_containers:
        container_name = container.container_id

        fractal_logger.info(
            "Beginning to delete unassigned container {container_name}. Goodbye!".format(
                container_name=container_name
            ),
            extra={"label": str(container_name)},
        )

        container = ensure_container_exists(container)
        if not container:
            # The container never existed, and has been deleted!
            continue

        # Initialize ECSClient
        container_cluster = container.cluster
        container_location = container.location

        # Database constraints ensure that we will only reach here if the cluster exists,
        # because otherwise no containers would be found in the DB.
        ecs_client = ECSClient(
            base_cluster=container_cluster, region_name=container_location, grab_logs=False
        )

        # Update ecs_client to use the current task
        ecs_client.add_task(container_name)

        # Actually stop the task if it's not done.
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
        # Delete the row for this container from the database.
        db.session.delete(container)

    # commit after all unassigned containers are deleted (release lock)
    db.session.commit()

    # Check if a cluster does not exist.
    ecs_client = ECSClient(launch_type="EC2", region_name=region_name)

    try:
        _, _, instance_list = ecs_client.update_cluster_with_new_ami(cluster_name, ami)
    except FractalECSClusterNotFoundException:
        # We should remove any entries in the DB referencing this cluster, as they are out of date
        # Cluster is a primary key, so `.first()` suffices
        bad_entry = ClusterInfo.query.filter_by(cluster=cluster_name).first()
        fractal_sql_commit(db, lambda db, x: db.session.delete(x), bad_entry)

        # Task was executed successfully; we have recorded the fact that this cluster did not exist.
        self.update_state(
            state="SUCCESS",
            meta={
                "msg": f"Cluster {cluster_name} in region {region_name} did not exist.",
            },
        )
    else:
        for instance_name in instance_list:
            instance = InstanceInfo.query.get(instance_name)
            instance.is_draining = True
            db.session.commit()
        # The cluster did exist, and was updated successfully.
        self.update_state(
            state="SUCCESS",
            meta={
                "msg": f"updated cluster {cluster_name} to ami {ami} in region {region_name}",
            },
        )


@shared_task(bind=True)
def update_region(self: Task, region_name: Optional[str] = "us-east-1", ami: Optional[str] = None):
    """
    Updates all clusters in a region to use a new AMI
    calls update_cluster under the hood

    Args:
        self (CeleryInstance): the celery instance running the task
        region_name (Optional[str]): which region the cluster is in
        ami (Optional[str]): which AMI to use

    Returns:
         None, though which cluster was updated is in celery state
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
        fractal_logger.info(
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
        fractal_logger.warning(
            f"No clusters found in region {region_name}",
        )
        self.update_state(
            state="SUCCESS",
            meta={"msg": f"No clusters in region {region_name}", "tasks": ""},
        )
        return

    fractal_logger.info(f"Updating clusters: {all_clusters}")
    tasks = []
    for cluster in all_clusters:
        task = update_cluster.delay(region_name, cluster.cluster, ami)
        tasks.append(task.id)

    fractal_logger.info(
        "update_region is returning with success. It spun up the "
        f"following update_cluster tasks: {tasks}"
    )

    # format tasks as space separated strings (used by AMI creation workflow to poll for success)
    delim = " "
    formatted_tasks = delim.join(tasks)

    self.update_state(
        state="SUCCESS",
        meta={"msg": f"updated to ami {ami} in region {region_name}", "tasks": formatted_tasks},
    )


@shared_task
def update_task_definitions(app_id: str = None, task_version: int = None):
    """
    Updates the task def for `app_id` to use version `task_version`.
    - If `app_id`=None, we update all app ids to their latest task_version.
    - If `app_id` is not None and `task_version` is not None, we simply write
        the new task_version to the db.

    Parallelizing this function could help reduce task time, but the AWS API responds
    in usually under a tenth of a second. We'd also need to coordinate that with
    Github workflows to make sure all tasks finish. It's much simpler to have just one task here.

    Args:
        app_id: which app id to update. If None, we update all in SupportedAppImages to
            their latest task_versions in AWS ECS.
        task_version: specific task_version to pin the task def associated with app_id.
    """
    # paramater validation
    if app_id is None and task_version is not None:
        raise InvalidArguments("Since app_id is None, task_version must be None")

    if app_id is None:
        # iterate through all apps and update their task definitions
        all_app_data = SupportedAppImages.query.all()
        # we must cast to python objects because the recursive call to update_task_definitions
        # does a db.session.commit, which invalidates existing objects. that causes the
        # second iteration of the for-loop to fail.
        all_app_ids = [app_data.app_id for app_data in all_app_data]
        for _app_id in all_app_ids:  # name is _app_id to not override app_id function arg
            update_task_definitions(app_id=_app_id)
        return

    # 30sec arbitrarily decided as sufficient timeout when using with_for_update
    set_local_lock_timeout(30)
    app_data = SupportedAppImages.query.filter_by(app_id=app_id).with_for_update().first()
    if app_data is None:
        raise InvalidAppId(f"App ID {app_id} is not in SupportedAppImages")
    if task_version is None:
        # use the task definition in the db, then get the latest task_version from AWS ECS.
        ecs_client = ECSClient()
        ecs_client.set_task_definition_arn(app_data.task_definition)
        task_info = ecs_client.describe_task()
        task_version = task_info["taskDefinition"]["revision"]
        app_data.task_version = task_version
    else:
        # we are given a specific task_version; write to row directly
        app_data.task_version = task_version

    # commit changes and log
    fractal_sql_commit(db)
    fractal_logger.info(
        f"App {app_data.app_id} has new task def: {app_data.task_definition}:{task_version}"
    )


@shared_task(bind=True)
def manual_scale_cluster(self, cluster: str, region_name: str):
    """
    Check scaling a cluster based on simple rules:
    1. expected_num_instances = ceil( (active_tasks + pending_tasks) / AWS_TASKS_PER_INSTANCE )
    2. If num_instances != expected_num_instances, change the cluster's ASG desired capacity to
        expected_num_instances.

    Args:
        cluster: cluster to manually scale
        region_name: region that cluster resides in
    """
    self.update_state(
        state="STARTED",
        meta={
            "msg": f"Checking if cluster {cluster} should be scaled.",
        },
    )

    factor = int(current_app.config["AWS_TASKS_PER_INSTANCE"])

    cluster_data = ClusterInfo.query.get(cluster)
    if cluster_data is None:
        raise InvalidCluster(f"Cluster {cluster} is not in ClusterInfo")

    num_tasks = cluster_data.pendingTasksCount + cluster_data.runningTasksCount
    num_instances = cluster_data.registeredContainerInstancesCount
    expected_num_instances = math.ceil(num_tasks / factor)

    # expected_num_instances must be >= cluster_data.minContainers
    expected_num_instances = max(expected_num_instances, cluster_data.minContainers)
    # expected_num_instances must be <= cluster_data.maxContainers
    expected_num_instances = min(expected_num_instances, cluster_data.maxContainers)

    if expected_num_instances == num_instances:
        fractal_logger.info(f"Cluster {cluster} did not need any scaling.")
        return

    fractal_logger.info(
        f"Changing ASG desired capacity in {cluster} from {num_instances} "
        f"to {expected_num_instances}."
    )
    ecs_client = ECSClient(launch_type="EC2", region_name=region_name)
    asg_list = ecs_client.get_auto_scaling_groups_in_cluster(cluster)
    if len(asg_list) != 1:
        raise ValueError(f"Expected 1 ASG but got {len(asg_list)} for cluster {cluster}")
    asg = asg_list[0]
    # This operation might not do anything sometimes. Ex:
    # if AWS puts 3 tasks on instance 1 and and 4 tasks on instance 2
    # no instance can be scaled-down, even though our algo says there should only be 1 instance
    # we have enabled instance termination protection in our ASGs and Capacity Providers
    # so any instance with tasks will not be killed. This has been manually tested.
    ecs_client.set_auto_scaling_group_capacity(asg, expected_num_instances)
