import math
import logging

from celery import shared_task

from app.helpers.utils.aws.base_ecs_client import ECSClient
from app.helpers.utils.general.logs import fractal_log
from app.models import (
    db,
    ClusterInfo,
    RegionToAmi,
)
from app.helpers.utils.general.sql_commands import fractal_sql_commit

from flask import current_app


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
    ecs_client = ECSClient(launch_type="EC2", region_name=region_name)
    ecs_client.update_cluster_with_new_ami(cluster_name, ami)

    self.update_state(
        state="SUCCESS",
        meta={
            "msg": (f"updating cluster {cluster_name} on ECS to ami {ami} in region {region_name}"),
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
    self.update_state(
        state="PENDING",
        meta={
            "msg": (f"updating to ami {ami} in region {region_name}"),
        },
    )

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
            meta={
                "msg": f"No clusters in region {region_name}",
            },
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

    self.update_state(
        state="SUCCESS",
        meta={
            "msg": f"updated to ami {ami} in region {region_name}",
        },
    )


@shared_task(bind=True)
def manual_scale_cluster(self, cluster: str, region_name: str):
    """
    Manually scales the cluster according the the following logic:
        - if num_tasks < AWS_TASKS_PER_INSTANCE * num_instances, set capacity to x such that
        AWS_TASKS_PER_INSTANCE * x < num_tasks < AWS_TASKS_PER_INSTANCE * (x + 1)

    FACTOR is hard-coded to 10 at the moment.
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

    factor = current_app.config["AWS_TASKS_PER_INSTANCE"]

    ecs_client = ECSClient(launch_type="EC2", region_name=region_name)
    cluster_data = ecs_client.describe_cluster(cluster)
    keys = [
        "runningTasksCount",
        "pendingTasksCount",
        "registeredContainerInstancesCount",
    ]
    for k in keys:
        if k not in cluster_data:
            raise ValueError(f"""Expected key {k} in AWS describle_cluster API response. 
                            Got: {cluster_data}""")
            
    num_tasks = cluster_data["runningTasksCount"] + cluster_data["pendingTasksCount"]
    num_instances = cluster_data["registeredContainerInstancesCount"]
    expected_num_instances = math.ceil(num_tasks/factor)

    if expected_num_instances == num_instances:
        self.update_state(
            state="SUCCESS",
            meta={
                "msg": f"Cluster {cluster} did not need any scaling.",
            },
        )

    # now we check if there are actually empty instances
    instances = ecs_client.list_container_instances(cluster)
    instances_tasks = ecs_client.describe_container_instances(cluster, instances)
    if num_instances != len(instances_tasks) != len(instances):
        # there could be a slight race condition here where an instance is spun-up
        # between the describe_cluster, list_container_instances, describe_container_instances
        # should we just not error check?
        raise ValueError(f"""Got {len(instances)} from list_container_instances and 
                            {len(instances)} from describe_container_instances""")

    empty_instances = 0
    for instance_task_data in instances_tasks:
        if "runningTasksCount" not in instance_task_data:
            raise ValueError(f"""Expected key {k} in AWS describe_container_instances 
                            API response: {instance_task_data}""")
        num_tasks = instance_task_data["runningTasksCount"]
        if num_tasks == 0:
            empty_instances += 1

    if empty_instances == 0:
        fractal_log(
            "manual_scale_cluster",
            None,
            f"""Cluster {cluster} had {num_instances} instances but should have 
                {expected_num_instances}. Number of total tasks: {num_tasks}. However, no instance 
                is empty so a scale down cannot be triggered. This means AWS ECS has suboptimally
                distributed tasks onto instances.""",
            level=logging.INFO,
        )

        self.update_state(
            state="SUCCESS",
            meta={
                "msg": """Detected that scale-down should happen but could 
                        not because there are no empty instances.""",
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
        ecs_client.set_auto_scaling_group_capacity(asg, num_instances - empty_instances)

        self.update_state(
            state="SUCCESS",
            meta={
                "msg": f"Scaled cluster {cluster} from {num_instances} to {num_instances - empty_instances}.",
            },
        )
    
