import io
import json
import os.path
import paramiko
import random
import string
import time
import uuid

from collections import defaultdict
from pprint import pprint

import boto3
import botocore.exceptions

from flask import current_app


def check_str_param(val, name):
    if isinstance(val, str):
        return val
    raise Exception("Value {} was supposed to be a string, but was a {}".format(name, type(val)))


def check_bool_param(val, name):
    if isinstance(val, bool):
        return val
    raise Exception("Value {} was supposed to be a boolean, but was a {}".format(name, type(val)))


class ECSClient:
    """
    This class governs everything you need to generate, run, and track a specific task on ECS
    Args:
        region_name (Optional[str]):  which AWS region you're running on
        key_id (Optional[str]): the AWS access key ID to use
        access_key (Optional[str]): the AWS access key going with that key_id
        launch_type (Optional[str]): whether to use EC2 or FARGATE for the task
        base_cluster (Optional[str]): what cluster to run on, used for testing/mocking
        grab_logs (Optional[bool]):  whether to pull down ECS logs
        starter_client (boto3.client): starter ecs client, used for mocking
        starter_log_client (boto3.client): starter log client, used for mocking
        starter_ec2_client (boto3.client): starter log client, used for mocking
        starter_s3_client (boto3.client): starter s3 client, used for mocking
        starter_ssm_client (boto3.client): starter ssm client, used for mocking
        starter_iam_client (boto3.client): starter iam client, used for mocking
        starter_auto_scaling_client (boto3.client): starter auto scaling client, used for mocking
    TODO: find ECS specific logs, if they exist -- right now logs are stdout and stderr
    """

    def __init__(
        self,
        region_name="us-east-1",
        key_id="",
        access_key="",
        launch_type="EC2",
        base_cluster=None,
        starter_client=None,
        starter_log_client=None,
        starter_ec2_client=None,
        starter_s3_client=None,
        starter_ssm_client=None,
        starter_iam_client=None,
        starter_auto_scaling_client=None,
        grab_logs=True,
        mock=False,
    ):
        self.key_id = check_str_param(key_id, "key_id")
        self.region_name = check_str_param(region_name, "region_name")
        self.access_key = check_str_param(access_key, "access_key")
        self.launch_type = check_str_param(launch_type, "launch_type")
        self.account_id = "123412341234"
        self.subnet = None
        self.vpc = None
        self.security_groups = None
        self.grab_logs = check_bool_param(grab_logs, "grab_logs")
        self.task_definition_arn = None
        self.tasks = []
        self.task_ips = {}
        self.task_ports = {}
        self.tasks_done = []
        self.offset = -1
        self.container_name = None
        self.ecs_logs = dict()
        self.logs_messages = dict()
        self.warnings = []
        self.cluster = None
        self.mock = mock

        if starter_client is None:
            self.ecs_client = self.make_client("ecs")
            self.account_id = boto3.client("sts").get_caller_identity().get("Account")
        else:
            self.ecs_client = starter_client
        if starter_log_client is None:
            self.log_client = self.make_client("logs")
        else:
            self.log_client = starter_log_client
        if starter_ec2_client is None:
            self.ec2_client = self.make_client("ec2")
        else:
            self.ec2_client = starter_ec2_client
        if starter_s3_client is None:
            self.s3_client = self.make_client("s3")
        else:
            self.s3_client = starter_s3_client
        if starter_ssm_client is None:
            self.ssm_client = self.make_client("ssm")
        else:
            self.ssm_client = starter_ssm_client
        if starter_iam_client is None:
            self.iam_client = self.make_client("iam")
        else:
            self.iam_client = starter_iam_client
        if starter_auto_scaling_client is None:
            self.auto_scaling_client = self.make_client("autoscaling")
        else:
            self.auto_scaling_client = starter_auto_scaling_client
        self.set_cluster(cluster_name=base_cluster)

        if not self.mock:
            self.role_name = "autoscaling_role"
            # pylint: disable=line-too-long
            # self.role_name = 'role_name_oqursxkjhh'
            ## Create role and instance profile that allows containers to use SSM, S3, and EC2
            # self.role_name = self.generate_name('role_name')
            # assume_role_policy_document = {
            #     "Version": "2012-10-17",
            #     "Statement": [
            #         {
            #             "Effect": "Allow",
            #             "Principal": {
            #                 "Service": [
            #                     "ec2.amazonaws.com"
            #                 ]
            #             },
            #             "Action": [
            #                 "sts:AssumeRole"
            #             ]
            #         }
            #     ]
            # }
            # self.iam_client.create_role(
            #     RoleName=self.role_name,
            #     AssumeRolePolicyDocument=json.dumps(assume_role_policy_document),
            # )
            # self.iam_client.attach_role_policy(
            #     PolicyArn='arn:aws:iam::aws:policy/AmazonSSMManagedInstanceCore',
            #     RoleName=self.role_name,
            # )
            # self.iam_client.attach_role_policy(
            #     PolicyArn='arn:aws:iam::aws:policy/AmazonS3FullAccess',
            #     RoleName=self.role_name,
            # )
            # self.iam_client.attach_role_policy(
            #     PolicyArn='arn:aws:iam::aws:policy/service-role/AmazonEC2ContainerServiceforEC2Role',
            #     RoleName=self.role_name,
            # )
            # self.iam_client.attach_role_policy(
            #     PolicyArn='arn:aws:iam::aws:policy/AmazonEC2FullAccess',
            #     RoleName=self.role_name,
            # )
            # self.iam_client.attach_role_policy(
            #     PolicyArn='arn:aws:iam::aws:policy/AmazonEC2ContainerServiceFullAccess',
            #     RoleName=self.role_name,
            # )
            # self.iam_client.attach_role_policy(
            #     PolicyArn='arn:aws:iam::aws:policy/AmazonECS_FullAccess',
            #     RoleName=self.role_name,
            # )
            self.instance_profile = "auto_scaling_instance_profile"
            # self.instance_profile = self.generate_name('instance_profile')
            # print(self.instance_profile)
            # self.iam_client.create_instance_profile(InstanceProfileName=self.instance_profile)
            # self.iam_client.add_role_to_instance_profile(
            #     InstanceProfileName=self.instance_profile, RoleName=self.role_name
            # )
            # pylint: enable=line-too-long

    def make_client(self, client_type, **kwargs):
        """
        Constructs an ECS client object with the given params
        Args:
            client_type (str): which ECS client you're trying to produce
            **kwargs: any add'l keyword params
        Returns:
            client: the constructed client object
        """
        clients = boto3.client(
            client_type,
            aws_access_key_id=(self.key_id if len(self.key_id) > 0 else None),
            aws_secret_access_key=(self.access_key if len(self.access_key) > 0 else None),
            region_name=(self.region_name if len(self.region_name) > 0 else None),
            **kwargs,
        )
        return clients

    def generate_name(self, starter_name=""):
        """
        Helper function for generating a name with a random UUID
        Args:
            starter_name (Optional[str]): starter string for the name
        Returns:
            str: the generated name
        """

        if current_app.testing:
            name = f"test-{starter_name.replace('_', '-')}-{uuid.uuid4()}"
        else:
            letters = string.ascii_lowercase
            name = starter_name + "_" + "".join(random.choice(letters) for i in range(10))

        return name

    def create_cluster(self, capacity_providers, cluster_name=None):
        """Create a new cluster with the specified capacity providers.

        Args:
            capacity_providers (List[str]): capacity providers to use for cluster
            cluster_name (Optional[str]): name of cluster, will be automatically generated if not
                provided
        """
        if isinstance(capacity_providers, str):
            capacity_providers = [capacity_providers]
        if not isinstance(capacity_providers, list):
            raise Exception("capacity_providers must be a list of strs")
        cluster_name = cluster_name or self.generate_name("cluster")
        self.ecs_client.create_cluster(
            clusterName=cluster_name,
            capacityProviders=capacity_providers,
            defaultCapacityProviderStrategy=[
                {"capacityProvider": capacity_provider, "weight": 1, "base": 0,}
                for capacity_provider in capacity_providers
            ],
        )
        self.set_cluster(cluster_name)
        return cluster_name

    def set_cluster(self, cluster_name=None):
        """Set the task's compute cluster.

        Choose either specified cluster or the first available/default compute cluster if a cluster
        is not provided.

        Args:
            cluster_name (Optional[str]): name of cluster to set task's compute cluster to
        """

        self.cluster = (
            check_str_param(cluster_name, "cluster_name")
            if cluster_name
            else self.ecs_client.list_clusters()["clusterArns"][0]
        )

    def get_all_clusters(self):
        """
        Returns:
            List[str]: list of all cluster ARNs owned by AWS account
        """
        clusters, next_token = [], None
        while True:
            clusters_response = (
                self.ecs_client.list_clusters(next_token)
                if next_token
                else self.ecs_client.list_clusters()
            )
            clusters.extend(clusters_response["clusterArns"])
            next_token = (
                clusters_response["nextToken"] if "nextToken" in clusters_response else None
            )
            if not next_token:
                break
        return clusters

    def describe_auto_scaling_groups_in_cluster(self, cluster):
        """
        Args:
            cluster (str): name of cluster to set task's compute cluster to
        Returns:
            List[Dict]: each dict contains details about an auto scaling group in the cluster
        """
        capacity_providers = self.ecs_client.describe_clusters(clusters=[cluster])["clusters"][0][
            "capacityProviders"
        ]
        capacity_providers_info = self.ecs_client.describe_capacity_providers(
            capacityProviders=capacity_providers
        )["capacityProviders"]
        auto_scaling_groups = list(
            map(
                lambda cp: cp["autoScalingGroupProvider"]["autoScalingGroupArn"].split("/")[-1],
                capacity_providers_info,
            )
        )
        return self.auto_scaling_client.describe_auto_scaling_groups(
            AutoScalingGroupNames=auto_scaling_groups
        )["AutoScalingGroups"]

    def get_container_instance_ips(self, cluster, containers):
        """
        Args:
            cluster (str): name of cluster
            containers (List[str]): either the full ARNs or ec2 instance IDs of the containers
        Returns:
            List[str]: the public IP address of each container
        """
        ec2_ids = self.get_container_instance_ids(cluster, containers)
        ec2_info = self.ec2_client.describe_instances(InstanceIds=ec2_ids)
        return list(
            map(
                lambda info: info.get("PublicIpAddress", -1),
                ec2_info["Reservations"][0]["Instances"],
            )
        )

    def get_container_instance_ids(self, cluster, containers):
        """
        Args:
            cluster (str): name of cluster
            containers (List[str]): either the full ARNs or ec2 instance IDs of the containers
        Returns:
            List[str]: the ec2 instance IDs of the containers
        """
        full_containers_info = self.ecs_client.describe_container_instances(
            cluster=cluster, containerInstances=containers
        )["containerInstances"]
        return list(map(lambda info: info["ec2InstanceId"], full_containers_info))

    def get_containers_in_cluster(self, cluster):
        """
        Args:
            cluster (str): name of cluster
        Returns:
            List[str]: the ARNs of the containers in the cluster
        """
        return self.ecs_client.list_container_instances(cluster=cluster)["containerInstanceArns"]

    def get_container_for_tasks(self, task_arns=None, cluster=None):
        if task_arns is None:
            task_arns = self.tasks
        if cluster is None:
            cluster = self.cluster
        resp = self.ecs_client.describe_tasks(cluster=cluster, tasks=task_arns)

        container_arns = [task["containerInstanceArn"] for task in resp["tasks"]]
        return container_arns

    def set_containers_to_draining(self, containers, cluster=None):
        """
        sets input list of containers to draining
        Args:
            containers (list[str]): the ARNs of the containers you want to drain
            cluster (optional[str]): the cluster on which the containers are, defaults to the
                overall client's cluster.

        Returns: the json returned by the API

        """
        if cluster is None:
            cluster = self.cluster
        resp = self.ecs_client.update_container_instances_state(
            cluster=cluster, containerInstances=containers, status="DRAINING"
        )
        return resp

    def terminate_containers_in_cluster(self, cluster):
        """Terminate all of the containers in the cluster.

        They will eventually be cleaned up automatically.

        Args:
            cluster (str): name of cluster
        """

        container_arns = self.get_containers_in_cluster(cluster)
        if container_arns:
            containers = self.get_container_instance_ids(cluster, container_arns)
            self.ec2_client.terminate_instances(InstanceIds=containers)

    def exec_commands_on_containers(self, cluster, containers, commands):
        """
        Runs shell commands on the specified containers in the cluster
        Args:
            cluster (str): name of cluster
            containers (List[str]): either the ARN or ec2 instance IDs of the containers
            commands (List[str]): shell commands to run on the containers
        """
        resp = self.ssm_client.send_command(
            DocumentName="AWS-RunShellScript",  # One of AWS' preconfigured documents
            Parameters={"commands": commands},
            InstanceIds=self.get_container_instance_ids(cluster, containers),
            OutputS3Region=self.region_name,
            OutputS3BucketName="fractal-container-outputs",
        )
        return resp

    def exec_commands_all_containers(self, commands):
        """
        Runs shell commands on all containers in all clusters associated with current AWS account
        Args:
            commands (List[str]): shell commands to run on the containers
        """
        clusters = self.get_all_clusters()
        for cluster in clusters:
            containers = self.get_containers_in_cluster(cluster)
            self.exec_commands_on_containers(cluster, containers, commands)

    def get_clusters_usage(self, clusters=None):
        """Fetch usage info of clusters.

        Fetched data includes status, pending tasks, running tasks, container instances, container
        usage, min containers, and max containers.

        Args:
            clusters (Optional[List[str]]): the clusters to get the usage info for, defaults to all
                clusters associated with AWS account

        Returns:
            Dict[Dict]: A dictionary mapping each cluster name to cluster usage info, which is
                stored in a dict.
        """

        clusters, clusters_usage = clusters or self.get_all_clusters(), {}
        for cluster in clusters:
            cluster_info = self.ecs_client.describe_clusters(clusters=[cluster])["clusters"][0]
            containers = self.get_containers_in_cluster(cluster)
            auto_scaling_group_info = self.describe_auto_scaling_groups_in_cluster(cluster)[0]
            max_resources = defaultdict(int)
            for container in containers:
                instance_info = self.ecs_client.describe_container_instances(
                    cluster=cluster, containerInstances=[container]
                )["containerInstances"][0]
                for resource in instance_info["remainingResources"]:
                    if resource["name"] == "CPU" or resource["name"] == "MEMORY":
                        max_resources[resource["name"]] = max(
                            max_resources[resource["name"]], resource["integerValue"]
                        )

            clusters_usage[cluster_info["clusterName"]] = {
                "status": cluster_info["status"],
                "pendingTasksCount": cluster_info["pendingTasksCount"],
                "runningTasksCount": cluster_info["runningTasksCount"],
                "registeredContainerInstancesCount": cluster_info[
                    "registeredContainerInstancesCount"
                ],
                "maxCPURemainingPerInstance": max_resources["CPU"],
                "maxMemoryRemainingPerInstance": max_resources["MEMORY"],
                "minContainers": auto_scaling_group_info["MinSize"],
                "maxContainers": auto_scaling_group_info["MaxSize"],
            }

        print(clusters_usage)
        return clusters_usage

    def set_and_register_task(
        self,
        command=None,
        entrypoint=None,
        basedict=None,
        port_mappings=None,
        family="echostart",
        containername="basictest",
        imagename=None,
        memory="512",
        cpu="256",
    ):
        """Generate a task.

        Generates a task corresponding to a given docker image, command, and entry point
        while setting instance attrs accordingly (logging and task attrs).

        Args:
            command (List[str]): Command to run on container
            entrypoint (List[str]): Entrypoint for container
            basedict (Optional[Dict[str, Any]]): the base parametrization of your task.
            port_mappings (Optional[List[Dict]): any port mappings you want on the host container,
                defaults to 8080 tcp.
            family (Optional[str]): what task family you want this task revising
            containername (Optional[str]):  what you want the container the task is on to be called
            imagename (Optional[str]): the URI for the docker image
            memory (Optional[str]): how much memory (in MB) the task needs
            cpu (Optional[str]): how much CPU (in vCPU) the task needs
        """

        fmtstr = family + str(self.offset + 1)
        if port_mappings is None:
            port_mappings = [{"hostPort": 8080, "protocol": "tcp", "containerPort": 8080}]
        base_log_config = {
            "logDriver": "awslogs",
            "options": {
                "awslogs-group": "/ecs/{}".format(fmtstr),
                "awslogs-region": self.region_name,
                "awslogs-stream-prefix": "ecs",
            },
        }
        try:
            self.log_client.create_log_group(logGroupName="/ecs/{}".format(fmtstr))
        except botocore.exceptions.ClientError as e:
            self.warnings.append(str(e))
        # TODO:  refactor this horrifying mess into multiple functions
        if basedict is None:
            basedict = {
                "executionRoleArn": "arn:aws:iam::{}:role/ecsTaskExecutionRole".format(
                    self.account_id
                ),
                "containerDefinitions": [
                    {
                        "logConfiguration": base_log_config,
                        "entryPoint": entrypoint,
                        "portMappings": port_mappings,
                        "command": command,
                        "cpu": 0,
                        "environment": [{"name": "TEST", "value": "end"}],
                        "image": (imagename if imagename is not None else "httpd:2.4"),
                        "name": containername,
                    }
                ],
                "placementConstraints": [],
                "memory": memory,
                "family": family,
                "networkMode": "bridge",
                "cpu": cpu,
            }
            if basedict["containerDefinitions"][0]["command"] is None:
                basedict["containerDefinitions"][0].pop("command")
            if basedict["containerDefinitions"][0]["entryPoint"] is None:
                basedict["containerDefinitions"][0].pop("entryPoint")
        else:
            container_params = basedict["containerDefinitions"][0]
            if command is not None:
                container_params["command"] = command
            if entrypoint is not None:
                container_params["entryPoint"] = entrypoint
            if imagename is not None:
                container_params["image"] = imagename
            container_params["name"] = containername
            container_params["portMappings"] = port_mappings
            container_params["logConfiguration"] = base_log_config
            basedict["family"] = family

        response = self.ecs_client.register_task_definition(**basedict)
        arn = response["taskDefinition"]["taskDefinitionArn"]
        self.task_definition_arn = arn
        self.container_name = containername

    def set_task_definition_arn(self, task_arn):
        self.task_definition_arn = task_arn

    def add_task(self, task_arn):
        """Add a task by ARN to this client's task list.

        Useful for keeping an eye on already running tasks.

        Args:
            task_arn: the ID of the task to add.
        """

        self.tasks.append(task_arn)
        self.tasks_done.append(False)
        self.offset += 1

    def run_task(self, use_launch_type=True, **kwargs):
        """
        sets this client's task running.
        TODO: explicitly add overrides as params here for cpu, command, and environment vars
        Args:
            **kwargs: Any add'l params you want the task to have
        """
        task_args = {"taskDefinition": self.task_definition_arn, "cluster": self.cluster}
        if use_launch_type:
            task_args["launchType"] = self.launch_type
        taskdict = self.ecs_client.run_task(**task_args, **kwargs)
        print(taskdict)
        task = taskdict["tasks"][0]
        running_task_arn = task["taskArn"]
        self.tasks.append(running_task_arn)
        self.tasks_done.append(False)
        self.offset += 1

    def stop_task(self, reason="user stopped", offset=0):
        self.ecs_client.stop_task(
            cluster=self.cluster, task=(self.tasks[offset]), reason=reason,
        )

    def create_launch_configuration(
        self,
        instance_type="g3s.xlarge",
        ami="ami-0c82e2febb87e6d1c",
        launch_config_name=None,
        cluster_name=None,
        key_name="auto-scaling-key",
    ):
        """Add a launch configuration to our AWS account.

        Args:
            instance_type (Optional[str]): size of instances to create in auto scaling group,
                defaults to t2.small
            ami (Optional[str]): AMI to use for the instances created in auto scaling group,
                defaults to an ECS-optimized, GPU-optimized Amazon Linux 2 AMI
            launch_config_name (Optional[str]): the name to give the generated launch configuration,
                will be automatically generated if not provided
            cluster_name (Optional[str]): the cluster name that the launch configuration will be
                used for, will be automatically generated if not provided

        Returns:
            (str, str): name of cluster for launch configuration, name of launch configuration
                created
        """

        cluster_name = cluster_name or self.generate_name("cluster")

        # Initial data/scripts to be run on all container instances
        with open(os.path.join(os.path.dirname(__file__), "base_userdata_template.sh")) as f:
            userdata = f.read().format(cluster_name)

        launch_config_name = launch_config_name or self.generate_name("launch_configuration")
        _ = self.auto_scaling_client.create_launch_configuration(
            LaunchConfigurationName=launch_config_name,
            ImageId=ami,
            InstanceType=instance_type,
            IamInstanceProfile=self.instance_profile,
            UserData=userdata,
            KeyName=key_name,
        )
        return cluster_name, launch_config_name

    def create_auto_scaling_group(
        self,
        launch_config_name,
        auto_scaling_group_name=None,
        min_size=0,
        max_size=10,
        availability_zones=None,
    ):
        """
        Args:
             launch_config_name (str): the launch configuration to use for the instances created by the auto scaling group
             auto_scaling_group_name (Optional[str]): the name to give the generated auto scaling group, will be automatically generated if not provided
             min_size (Optional[int]): the minimum number of containers in the auto scaling group, defaults to 1
             max_size (Optional[int]): the maximum number of containers in the auto scaling group, defaults to 10
             availability_zones (Optional[List[str]]): the availability zones for creating instances in the auto scaling group
        Returns:
             str: name of auto scaling group created
        """
        availability_zones = availability_zones or [
            self.region_name + "a" if self.region_name != "us-east-1" else "us-east-1b"
        ]
        if isinstance(availability_zones, str):
            availability_zones = [availability_zones]
        if not isinstance(availability_zones, list):
            raise Exception("availability_zones should be a list of strs")
        auto_scaling_group_name = auto_scaling_group_name or self.generate_name(
            "auto_scaling_group"
        )
        _ = self.auto_scaling_client.create_auto_scaling_group(
            AutoScalingGroupName=auto_scaling_group_name,
            LaunchConfigurationName=launch_config_name,
            MaxSize=max_size,
            MinSize=min_size,
            AvailabilityZones=availability_zones,
        )

        return auto_scaling_group_name

    def create_capacity_provider(self, auto_scaling_group_name, capacity_provider_name=None):
        """
        Args:
             auto_scaling_group_name (str): the auto scaling group to create a capacity provider for
             capacity_provider_name (Optional[str]): the name to give the generated capacity provider, will be automatically generated if not provided
        Returns:
             str: name of capacity provider created
        """
        auto_scaling_group_info = self.auto_scaling_client.describe_auto_scaling_groups(
            AutoScalingGroupNames=[auto_scaling_group_name]
        )
        auto_scaling_group_arn = auto_scaling_group_info["AutoScalingGroups"][0][
            "AutoScalingGroupARN"
        ]
        capacity_provider_name = capacity_provider_name or self.generate_name("capacity_provider")
        _ = self.ecs_client.create_capacity_provider(
            name=capacity_provider_name,
            autoScalingGroupProvider={
                "autoScalingGroupArn": auto_scaling_group_arn,
                "managedScaling": {
                    "maximumScalingStepSize": 1000,
                    "minimumScalingStepSize": 1,
                    "status": "ENABLED",
                    "targetCapacity": 70,
                },
            },
        )
        return capacity_provider_name

    def get_task_network_info(self, task_info, desired_detail):
        for attachment in task_info["attachments"]:
            if attachment["type"] == "ElasticNetworkInterface":
                for detail in attachment["details"]:
                    print(detail["name"])
                    if detail["name"] == desired_detail:
                        return detail["value"]
        raise NotImplementedError

    def get_task_binding_info(self, task_info):
        network_binding_map = {}
        for container in task_info["containers"]:
            print(container)
            for network_binding in container["networkBindings"]:
                network_binding_map[network_binding["containerPort"]] = network_binding["hostPort"]
        return network_binding_map

    def get_task_ip_ports(self, offset=0):
        if self.tasks_done[offset]:
            return False
        response = self.ecs_client.describe_tasks(tasks=self.tasks, cluster=self.cluster)
        resp = response["tasks"][offset]
        if resp["lastStatus"] == "RUNNING" or resp["lastStatus"] == "STOPPED":
            container_info = self.ecs_client.describe_container_instances(
                cluster=self.cluster, containerInstances=[resp["containerInstanceArn"]]
            )
            ec2_id = container_info["containerInstances"][0]["ec2InstanceId"]
            ec2_info = self.ec2_client.describe_instances(InstanceIds=[ec2_id])
            public_ip = ec2_info["Reservations"][0]["Instances"][0].get("PublicIpAddress", -1)
            self.task_ips[offset] = public_ip

            network_binding_map = self.get_task_binding_info(resp)
            if network_binding_map:
                self.task_ports[offset] = network_binding_map
                return True
        return False

    def pick_subnets(self):
        subnet_info = self.ec2_client.describe_subnets()
        for subnet in subnet_info["Subnets"]:
            if subnet["MapPublicIpOnLaunch"]:
                if self.vpc is None or self.vpc == subnet["VpcId"]:
                    self.subnet = subnet["SubnetId"]
                    self.vpc = subnet["VpcId"]
                    break

        return [self.subnet]

    def pick_security_groups(self):
        id_list = []
        security_group_info = self.ec2_client.describe_security_groups()
        for security_group in security_group_info["SecurityGroups"]:
            if security_group["Description"] == "default VPC security group":
                if self.vpc is None or self.vpc == security_group["VpcId"]:
                    id_list.append(security_group["GroupId"])
        self.security_groups = id_list
        return id_list

    def build_network_config(self):
        if self.vpc is None:
            self.get_vpc()
        if self.subnet is None or self.vpc is None:
            self.pick_subnets()
        if self.security_groups is None:
            self.pick_security_groups()
        networkConfiguration = {
            "awsvpcConfiguration": {
                "subnets": [self.subnet],
                "securityGroups": self.security_groups,
            }
        }
        return networkConfiguration

    def get_vpc(self):
        container_arns = self.ecs_client.list_container_instances(cluster=self.cluster)[
            "containerInstanceArns"
        ]
        # First try to get VPC from containers running on the cluster
        if container_arns:
            container = self.ecs_client.describe_container_instances(
                cluster=self.cluster, containerInstances=container_arns,
            )["containerInstances"][0]
            attributes = container["attributes"]
            for attribute in attributes:
                if attribute["name"] == "ecs.vpc-id":
                    self.vpc = attribute["value"]
                    return self.vpc
        else:
            auto_scaling_groups = self.describe_auto_scaling_groups_in_cluster(self.cluster)
            if not auto_scaling_groups:
                raise Exception("Cluster has no instances and no autoscaling group!")
            if "VPCZoneIdentifier" not in auto_scaling_groups[0]:
                raise Exception("Cluster autoscaling group has no VPC Zone Identifier!")
            subnet_ids = auto_scaling_groups[0]["VPCZoneIdentifier"].split(", ")
            self.subnet = subnet_ids[0]
            self.vpc = self.ec2_client.describe_subnets(SubnetIds=subnet_ids)["Subnets"][0]["VpcId"]
            return self.vpc

    def check_if_done(self, offset=0):
        """
        polls ECS to see if the task is complete, pulling down logs if so
        TODO:  handle pagination for 100+ tasks
        Args:
             offset (int): which task to check for
        Returns:
             boolean corresponding to completion
        """
        if self.tasks_done[offset]:
            return True
        response = self.ecs_client.describe_tasks(tasks=self.tasks, cluster=self.cluster)
        resp = response["tasks"][offset]
        if resp["lastStatus"] == "STOPPED":
            if self.grab_logs:
                check_arn = resp["taskDefinitionArn"]
                taskDefinition = self.ecs_client.describe_task_definition(taskDefinition=check_arn)[
                    "taskDefinition"
                ]
                logconfig = taskDefinition["containerDefinitions"][0]["logConfiguration"]["options"]
                streamobj = self.log_client.describe_log_streams(
                    logGroupName=logconfig["awslogs-group"],
                    logStreamNamePrefix=logconfig["awslogs-stream-prefix"],
                )
                logs = self.log_client.get_log_events(
                    logGroupName=logconfig["awslogs-group"],
                    logStreamName=streamobj["logStreams"][-1]["logStreamName"],
                )
                self.ecs_logs[offset] = logs["events"]
                self.logs_messages[offset] = [log["message"] for log in logs["events"]]
            self.tasks_done[offset] = True
            return True
        return False

    def spin_til_running(self, offset=0, time_delay=5):
        while not self.get_task_ip_ports(offset=offset):
            time.sleep(time_delay)

    def spin_til_containers_up(self, cluster_name, time_delay=5):
        """
        spinpolls until container instances have been assigned to the cluster
        Args:
            cluster_name (str)
            time_delay (int): how long to wait between polls, seconds
        """
        container_instances = []
        while not container_instances:
            container_instances = self.get_containers_in_cluster(cluster_name)
            time.sleep(time_delay)

        # wait another 30 seconds just to be safe
        time.sleep(30)
        return container_instances

    def spin_til_no_containers(self, cluster_name, time_delay=5):
        """
        spinpolls until all container instances have been removed from the cluster
        Args:
            cluster_name (str)
            time_delay (int): how long to wait between polls, seconds
        """
        container_instances = []
        while container_instances:
            container_instances = self.get_containers_in_cluster(cluster_name)
            time.sleep(time_delay)

        # wait another 30 seconds just to be safe
        time.sleep(30)

    def spin_til_command_executed(self, command_id, time_delay=5):
        """
        spinpolls until command has been executed
        Args:
            command_id (str): the command to wait on
            time_delay (int): how long to wait between polls, seconds
        Returns:
            bool: True if the command executed successfully
        """
        while True:
            status = self.ssm_client.list_commands(CommandId=command_id)["Commands"][0]["Status"]
            if status == "Success":
                return True
            if status == "Pending" or status == "InProgress":
                time.sleep(time_delay)
            else:
                return False

    def spin_til_done(self, offset=0, time_delay=5):
        """
        spinpolls the ECS servers to test if the task is done
        Args:
            offset (int): the offset of the task within the task array
            time_delay (int): how long to wait between polls, seconds
        """
        while not self.check_if_done(offset=offset):
            time.sleep(time_delay)

    def spin_all(self, time_delay=5):
        """
        spinpolls all generated tasks
        Args:
            time_delay (int): how long to wait between polls, seconds
        """
        for i in range(self.offset + 1):
            self.spin_til_done(offset=i, time_delay=time_delay)

    def run_all(self, time_delay=5):
        for i in range(self.offset + 1):
            self.spin_til_running(offset=i, time_delay=time_delay)

    def create_auto_scaling_cluster(
        self,
        cluster_name=None,
        instance_type="t2.small",
        ami="ami-04cfcf6827bb29439",
        min_size=0,
        max_size=10,
        availability_zones=None,
    ):
        """
        Creates launch configuration, auto scaling group, capacity provider, and cluster
        Args:
            instance_type (Optional[str]): size of instances to create in auto scaling group, defaults to t2.small
            ami (Optional[str]): AMI to use for the instances created in auto scaling group, defaults to an ECS-optimized, GPU-optimized Amazon Linux 2 AMI
            min_size (Optional[int]): the minimum number of containers in the auto scaling group, defaults to 1
            max_size (Optional[int]): the maximum number of containers in the auto scaling group, defaults to 10
            region_name (Optional[str]): which AWS region you're running on
            availability_zones (Optional[List[str]]): the availability zones for creating instances in the auto scaling group
        Returns:
            (str, str, str, str): cluster name, launch configuration name, auto scaling group name, capacity provider name
        """
        cluster_name, launch_config_name = self.create_launch_configuration(
            instance_type=instance_type, ami=ami, cluster_name=cluster_name
        )
        auto_scaling_group_name = self.create_auto_scaling_group(
            launch_config_name=launch_config_name,
            min_size=min_size,
            max_size=max_size,
            availability_zones=availability_zones,
        )
        capacity_provider_name = self.create_capacity_provider(
            auto_scaling_group_name=auto_scaling_group_name
        )
        self.create_cluster(capacity_providers=[capacity_provider_name], cluster_name=cluster_name)
        return cluster_name, launch_config_name, auto_scaling_group_name, capacity_provider_name


if __name__ == "__main__":
    # testclient = ECSClient(base_cluster='cluster_eqbpomqrnp')
    # pprint(
    #     testclient.ecs_client.describe_container_instances(
    #         cluster=testclient.cluster,
    #         containerInstances=testclient.ecs_client.list_container_instances(
    #             cluster=testclient.cluster
    #         )['containerInstanceArns'],
    #     )
    # )

    testclient = ECSClient(region_name="us-east-1")
    # instance_profiles = testclient.iam_client.list_instance_profiles()['InstanceProfiles']
    # for instance_profile in instance_profiles:
    #     if 'instance_profile_' in instance_profile['InstanceProfileName']:
    #         print(instance_profile['InstanceProfileName'])
    #         for role in instance_profile['Roles']:
    #             testclient.iam_client.remove_role_from_instance_profile(RoleName=role['RoleName'], InstanceProfileName=instance_profile['InstanceProfileName'])
    #         testclient.iam_client.delete_instance_profile(InstanceProfileName=instance_profile['InstanceProfileName'])

    clusters = testclient.ecs_client.list_clusters()["clusterArns"]
    clusters_info = testclient.ecs_client.describe_clusters(clusters=clusters)["clusters"]
    for cluster_info in clusters_info:
        print(cluster_info)
        if (
            "cluster_" in cluster_info["clusterName"]
            and not cluster_info["registeredContainerInstancesCount"]
            and not cluster_info["pendingTasksCount"]
        ):
            testclient.ecs_client.delete_cluster(cluster=cluster_info["clusterName"])

    auto_scaling_instances = testclient.auto_scaling_client.describe_auto_scaling_instances()[
        "AutoScalingInstances"
    ]
    for auto_scaling_instance in auto_scaling_instances:
        print(auto_scaling_instance)
        try:
            testclient.auto_scaling_client.terminate_instance_in_auto_scaling_group(
                InstanceId=auto_scaling_instance["InstanceId"], ShouldDecrementDesiredCapacity=False
            )
        except:
            continue
    auto_scaling_groups = testclient.auto_scaling_client.describe_auto_scaling_groups()[
        "AutoScalingGroups"
    ]
    for auto_scaling_group in auto_scaling_groups:
        print(auto_scaling_group)
        try:
            testclient.auto_scaling_client.delete_auto_scaling_group(
                AutoScalingGroupName=auto_scaling_group["AutoScalingGroupName"]
            )
        except:
            continue
