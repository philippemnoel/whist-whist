import subprocess
import json
import sys
from datetime import datetime, timedelta, timezone
import dateutil.parser

# Uses AWS CLI to return all Auto Scaling Groups in AWS for a specified region
def get_all_aws_asgs(region):
    all_asgs, _ = subprocess.Popen(
        [
            "aws",
            "autoscaling",
            "describe-auto-scaling-groups",
            "--no-paginate",
            "--region",
            region,
        ],
        stdout=subprocess.PIPE,
    ).communicate()
    all_asgs = [
        asg["AutoScalingGroupARN"] for asg in json.loads(all_asgs)["AutoScalingGroups"]
    ]
    return all_asgs


# Uses AWS CLI to return all clusters in AWS for a specified region
def get_all_aws_clusters(region):
    all_cluster_arns, _ = subprocess.Popen(
        ["aws", "ecs", "list-clusters", "--no-paginate", "--region", region],
        stdout=subprocess.PIPE,
    ).communicate()
    all_cluster_arns = json.loads(all_cluster_arns)["clusterArns"]
    return all_cluster_arns


# Takes a Cluster name and returns the number of Container Instances
def get_num_instances(cluster, region):
    instances, _ = subprocess.Popen(
        [
            "aws",
            "ecs",
            "describe-clusters",
            "--no-paginate",
            "--region",
            region,
            "--clusters",
            cluster,
        ],
        stdout=subprocess.PIPE,
    ).communicate()
    instance_count = json.loads(instances)["clusters"][0][
        "registeredContainerInstancesCount"
    ]
    return instance_count


# Takes a Cluster ARN and maps it to its Auto Scaling Group(s) via its
# associated Capacity Provider(s)
def cluster_to_asgs(cluster_arn, region):
    capacity_providers, _ = subprocess.Popen(
        [
            "aws",
            "ecs",
            "describe-clusters",
            "--no-paginate",
            "--region",
            region,
            "--clusters",
            cluster_arn,
        ],
        stdout=subprocess.PIPE,
    ).communicate()
    capacity_providers = json.loads(capacity_providers)["clusters"][0][
        "capacityProviders"
    ]
    if len(capacity_providers) > 0:
        asgs, _ = subprocess.Popen(
            [
                "aws",
                "ecs",
                "describe-capacity-providers",
                "--no-paginate",
                "--region",
                region,
                "--capacity-providers",
                " ".join(capacity_providers),
            ],
            stdout=subprocess.PIPE,
        ).communicate()
        asgs = [
            asg["autoScalingGroupProvider"]["autoScalingGroupArn"]
            for asg in json.loads(asgs)["capacityProviders"]
            if "autoScalingGroupProvider" in asg
        ]
        return asgs
    else:
        return []


# Queries specified db for all clusters in a specified region
def get_db_clusters(url, secret, region):
    clusters, _ = subprocess.Popen(
        [
            "curl",
            "-L",
            "-X",
            "POST",
            url,
            "-H",
            "Content-Type: application/json",
            "-H",
            "x-hasura-admin-secret: %s" % (secret),
            "--data-raw",
            (
                '{"query":"query get_clusters($_eq: String = \\"%s\\")'
                ' { hardware_cluster_info(where: {location: {_eq: $_eq}}) { cluster }}"}'
            )
            % (region),
        ],
        stdout=subprocess.PIPE,
    ).communicate()
    clusters = json.loads(clusters)["data"]["hardware_cluster_info"]
    return [list(cluster.values())[0] for cluster in clusters]


# Queries specified db for all tasks in a specified region
def get_db_tasks(url, secret, region):
    tasks, _ = subprocess.Popen(
        [
            "curl",
            "-L",
            "-X",
            "POST",
            url,
            "-H",
            "Content-Type: application/json",
            "-H",
            "x-hasura-admin-secret: %s" % (secret),
            "--data-raw",
            (
                '{"query":"query get_tasks($_eq: String = \\"%s\\")'
                ' { hardware_user_containers(where: {location: {_eq: $_eq}}) { container_id }}"}'
            )
            % (region),
        ],
        stdout=subprocess.PIPE,
    ).communicate()
    tasks = json.loads(tasks)["data"]["hardware_user_containers"]
    return [list(task.values())[0] for task in tasks]


# Compares list of ASGs in AWS to list of ASGs associated with Clusters in AWS
def get_hanging_asgs(region):
    cluster_asgs = [
        asg
        for cluster_arn in get_all_aws_clusters(region)
        for asg in cluster_to_asgs(cluster_arn, region)
        if cluster_arn is not None
    ]
    all_asgs = get_all_aws_asgs(region)

    # return names (not ARNs) of ASGs not assocated a cluster (could be more
    # than one ASG per cluster)
    return [asg.split("/")[-1] for asg in list(set(all_asgs) - set(cluster_asgs))]


# Compares list of clusters in AWS to list of clusters in all DBs
def get_hanging_clusters(urls, secrets, region):
    # parses names of clusters from ARNs since only the names are stored in the DB
    aws_clusters = set(
        [cluster.split("/")[-1] for cluster in get_all_aws_clusters(region)]
    )
    if "default" in aws_clusters:
        aws_clusters.remove("default")

    # loop through each of dev, staging, and prod; add clusters to db_clusters
    db_clusters = set()
    for url, secret in zip(urls, secrets):
        db_clusters |= set(get_db_clusters(url, secret, region))

    # return list of cluster names in aws but not in db (and vice versa),
    # ignoring the 'default' cluster
    return [list(aws_clusters - db_clusters), list(db_clusters - aws_clusters)]


# Determines if provided task is older than one day and stops it using the AWS CLI
def delete_if_older_than_one_day(task, cluster, time):
    now = datetime.now(timezone.utc)
    then = dateutil.parser.parse(time)
    if now - then > timedelta(days=1):
        _, _ = subprocess.Popen(
            [
                "aws",
                "ecs",
                "stop-task",
                "--cluster",
                cluster,
                "--task",
                task,
                "--reason",
                "Automatically stopped by resource cleanup script (older than 1 day).",
            ],
            stdout=subprocess.PIPE,
        ).communicate()
        return " (stop triggered)"
    return ""


# Compares list of tasks associated with clusters in AWS to list of tasks in all DBs
def get_hanging_tasks(urls, secrets, region):
    # query dbs for tasks and clusters
    db_clusters = []
    db_tasks = set()
    for url, secret in zip(urls, secrets):
        db_clusters += get_db_clusters(url, secret, region)
        db_tasks |= set(get_db_tasks(url, secret, region))

    aws_tasks = set()
    aws_tasks_and_times = {}
    for cluster in db_clusters:
        # verify cluster exists in aws (prevents later exception)
        response, _ = subprocess.Popen(
            [
                "aws",
                "ecs",
                "describe-clusters",
                "--no-paginate",
                "--region",
                region,
                "--cluster",
                cluster,
            ],
            stdout=subprocess.PIPE,
        ).communicate()
        if len(json.loads(response)["clusters"]) > 0:
            # get all tasks in a cluster (if it exists)
            tasks, _ = subprocess.Popen(
                [
                    "aws",
                    "ecs",
                    "list-tasks",
                    "--no-paginate",
                    "--region",
                    region,
                    "--cluster",
                    cluster,
                ],
                stdout=subprocess.PIPE,
            ).communicate()
            tasks = json.loads(tasks)["taskArns"]
            aws_tasks |= set(tasks)
            # if there are tasks in a cluster, get created time
            if len(tasks) > 0:
                task_times, _ = subprocess.Popen(
                    [
                        "aws",
                        "ecs",
                        "describe-tasks",
                        "--no-paginate",
                        "--region",
                        region,
                        "--cluster",
                        cluster,
                        "--tasks",
                    ]
                    + tasks,
                    stdout=subprocess.PIPE,
                ).communicate()
                tasks_and_times = (
                    (task["taskArn"], (cluster, task["createdAt"]))
                    for task in json.loads(task_times)["tasks"]
                )
                aws_tasks_and_times.update(dict(tasks_and_times))

    # determine which tasks are in aws but not in the db and return them (and their creation time)
    hanging_tasks = list(aws_tasks - db_tasks)
    return [
        (
            task,
            delete_if_older_than_one_day(
                task, aws_tasks_and_times[task][0], aws_tasks_and_times[task][1]
            ),
        )
        for task in hanging_tasks
    ]


def main():
    component = sys.argv[1]
    region = sys.argv[2]
    urls = sys.argv[3:6]
    secrets = sys.argv[6:9]

    # format as bulleted list for Slack notification
    if component == "ASGs":
        asgs = get_hanging_asgs(region)
        if len(asgs) > 0:
            print("\\n- `" + "`\\n- `".join([str(x) for x in asgs]) + "`")
    elif component == "Clusters":
        output = []
        aws_clusters, db_clusters = get_hanging_clusters(urls, secrets, region)
        for cluster in aws_clusters:
            output.append((str(cluster), get_num_instances(cluster, region)))
        if len(aws_clusters) > 0:
            print(
                "\\n- "
                + "\\n- ".join(
                    [
                        "`"
                        + c
                        + "`"
                        + " in AWS but not in any DB ("
                        + str(n)
                        + " instances)"
                        for c, n in output
                    ]
                )
            )
        if len(db_clusters) > 0:
            print(
                "\\n- "
                + "\\n- ".join(
                    ["`" + c + "`" + " in a DB but not AWS" for c in db_clusters]
                )
            )
    elif component == "Tasks":
        tasks = get_hanging_tasks(urls, secrets, region)
        if len(tasks) > 0:
            print("\\n- " + "\\n- ".join(["`" + str(t) + "`" + d for t, d in tasks]))


if __name__ == "__main__":
    main()
