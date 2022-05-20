#!/usr/bin/env python3

import os
import sys
import time
import json
import boto3
from datetime import datetime, timedelta

# add the current directory to the path no matter where this is called from
sys.path.append(os.path.join(os.getcwd(), os.path.dirname(__file__), "."))


def parse_metadata(folder_name):
    """
    Obtain the metadata for the E2E streaming test from the experiment_metadata.json file in the logs folder

    Args:
        folder_name (str):  The path to the folder containing the logs and the experiment_metadata.json file

    Returns:
        On success:
            experiment_metadata (dict): A dictionary with the key:value metadata pairs retrieved from the json file
        On failure:
            None
    """
    metadata_filename = os.path.join(folder_name, "experiment_metadata.json")
    experiment_metadata = None
    if not os.path.isfile(metadata_filename):
        print(f"Metadata file {metadata_filename} does not exist")
    else:
        with open(metadata_filename, "r") as metadata_file:
            experiment_metadata = json.load(metadata_file)
    return experiment_metadata


def logs_contain_errors(logs_root_dir, verbose=False):
    """
    Perform a coarse-grained sanity check on the logs from a E2E streaming test run

    Args:
        logs_root_dir (str):    The path to the folder containing the logs
        verbose (bool): Whether to print verbose logs to stdout

    Returns:
        False if the logs appear not to contain errors, True otherwise
    """
    # Check if the log files with metrics are present
    client_log_file = os.path.join(logs_root_dir, "client", "client.log")
    server_log_file = os.path.join(logs_root_dir, "server", "server.log")

    if not os.path.isfile(client_log_file) or not os.path.isfile(server_log_file):
        if verbose:
            print(f"File {client_log_file} and/or {server_log_file} do not exist!")
        return True
    client_log_num_lines = sum(1 for x in open(client_log_file))
    server_log_num_lines = sum(1 for x in open(server_log_file))

    # When the logging files are too short, this very likely means that the client failed to connect to the server
    if client_log_num_lines < 500 or server_log_num_lines < 500:
        if verbose:
            print(
                f"Warning: client log file {client_log_file} contains {client_log_num_lines} lines and server log file {server_log_file} contains {server_log_num_lines} lines. At least one of them is too short, which likely means the client failed to connect to the server."
            )
        return True

    return False


def download_latest_logs(
    branch_name,
    before_timestamp,
    logs_expiration_days,
    network_conditions,
    network_conditions_matching_way,
    testing_url,
    testing_time,
    simulate_scrolling,
    using_two_instances,
    verbose=False,
):
    """
    Download from S3 the most recent logs from a E2E streaming test run. Filter out runs that
    do not match the branch of interest, or that happened at or after a specific date/time.
    In addition, filter out runs based on network conditions depending on the desired matching way.
    If network_conditions_matching_way is 'match', then filter out all logs from runs whose network
    conditions are not identical to the network_conditions parameter to this function.
    If network_conditions_matching_way is 'normal_only', then filter out all logs from runs whose
    network conditions are not set to 'normal'. If network_conditions_matching_way is 'do_not_care',
    then do not filter out logs based on network conditions

    Args:
        branch_name (str):  The name of the Whist branch whose logs we will download
        before_timestamp (datetime):    The earliest timestamp such that if a run started at that time,
                                        we don't want to consider its logs
        logs_expiration_days (int): The number of days after which existing logs are considered obsolete
                                    and should not be considered for comparisons
        network_conditions (str):   The network conditions of the run that we just completed.
        network_conditions_matching_way (str):  A parameter that controls what kind of matching we
                                                need to do on the S3 logs wrt network conditions
        testing_url (str):  The url visited during the E2E run that we just completed.
        testing_time (int): The duration of the E2E test in the run that we just completed.
        simulate_scrolling (int):   The number of rounds of scrolling that was simulated on the client
                                    side in the E2E run that we just completed.
        using_two_instances (bool): Whether the latest E2E run was run using one or two instances
        verbose (bool): Whether to print verbose logs to stdout

    Returns:
        None
    """
    client = boto3.client("s3")
    s3_resource = boto3.resource("s3")
    bucket = s3_resource.Bucket("whist-e2e-protocol-test-logs")

    if os.path.exists(branch_name):
        os.system(f"rm -rf {branch_name}")

    os.makedirs(os.path.join(".", branch_name, "client"))
    os.makedirs(os.path.join(".", branch_name, "server"))
    compared_client_log_path = os.path.join(".", branch_name, "client", "client.log")
    compared_server_log_path = os.path.join(".", branch_name, "server", "server.log")
    exp_meta_path = os.path.join(".", branch_name, "experiment_metadata.json")

    local_timezone = int(time.timezone / 3600.0)
    before_timestamp = before_timestamp + timedelta(hours=local_timezone)
    # Compute oldest allowed timestamp. Any logs created before then will be discarded.
    oldest_allowed_timestamp = before_timestamp - timedelta(days=logs_expiration_days)

    result = client.list_objects(
        Bucket="whist-e2e-protocol-test-logs", Prefix=f"{branch_name}/", Delimiter="/"
    )

    folders = result.get("CommonPrefixes")
    if folders is None:
        print(f"Warning, S3 does not contain logs for branch {branch_name}")
        return
    counter = 1
    reason_for_discarding = []
    subfolder_date = ""
    for folder_name in reversed(folders):
        subfolder_name = folder_name.get("Prefix").split("/")[-2]

        # Make sure that we are comparing this run to a previous run
        subfolder_date = datetime.strptime(subfolder_name, "%Y_%m_%d@%H-%M-%S")
        if subfolder_date >= before_timestamp:
            counter += 1
            reason_for_discarding.append(
                (subfolder_date, "logs created at a time >= the current run's start time")
            )
            continue
        elif subfolder_date < oldest_allowed_timestamp:
            counter += 1
            reason_for_discarding.append(
                (subfolder_date, f"logs older than {logs_expiration_days} days")
            )
            continue

        for obj in bucket.objects.filter(Prefix=f"{branch_name}/{subfolder_name}"):
            if "client.log" in obj.key:
                bucket.download_file(obj.key, compared_client_log_path)
            elif "server.log" in obj.key:
                bucket.download_file(obj.key, compared_server_log_path)
            elif "experiment_metadata.json" in obj.key:
                bucket.download_file(obj.key, exp_meta_path)

        # Check if logs are sane, if so stop
        if logs_contain_errors(os.path.join(".", branch_name)):
            os.system(
                f"rm -f {compared_client_log_path} {compared_server_log_path} {exp_meta_path}"
            )
            counter += 1
            reason_for_discarding.append((subfolder_date, "errors in logs"))
            continue

        # Get the metadata for the compared run. If it does not exist, continue
        if not os.path.isfile(exp_meta_path) or not parse_metadata(os.path.join(".", branch_name)):
            os.system(
                f"rm -f {compared_client_log_path} {compared_server_log_path} {exp_meta_path}"
            )
            counter += 1
            reason_for_discarding.append((subfolder_date, "lack of metadata"))
            continue

        compared_run_meta = parse_metadata(os.path.join(".", branch_name))
        compared_network_conditions = compared_run_meta.get("network_conditions")
        compared_testing_url = compared_run_meta.get("testing_url")
        compared_testing_time = compared_run_meta.get("testing_time")
        compared_simulate_scrolling = compared_run_meta.get("simulate_scrolling")
        compared_using_two_instances = compared_run_meta.get("using_two_instances")

        # If the E2E test conditions match and the network conditions of the run in question are what we want, we are done. Otherwise, try with another set of logs
        if (
            testing_url == compared_testing_url
            and testing_time == compared_testing_time
            and simulate_scrolling == compared_simulate_scrolling
            and using_two_instances == compared_using_two_instances
        ) and (
            (
                network_conditions_matching_way == "match"
                and network_conditions == compared_network_conditions
            )
            or (
                network_conditions_matching_way == "normal_only"
                and compared_network_conditions == "normal"
            )
            or (network_conditions_matching_way == "do_not_care")
        ):
            break

        os.system(f"rm -f {compared_client_log_path} {compared_server_log_path} {exp_meta_path}")
        counter += 1
        reason_for_discarding.append((subfolder_date, "network conditions mismatch"))

    if counter > 1:
        if counter > len(folders):
            print(
                f"Warning: could not find any logs from branch {branch_name} with the required properties for comparison."
            )
        else:
            print(
                f"Using {counter}° most recent logs (time: {subfolder_date}) from branch {branch_name}"
            )
        assert counter == len(reason_for_discarding) + 1
        if verbose:
            for i in range(len(reason_for_discarding)):
                print(
                    f"\t {i + 1}° most recent logs (time: {reason_for_discarding[i][0]}) discarded. Reason: {reason_for_discarding[i][1]}"
                )
