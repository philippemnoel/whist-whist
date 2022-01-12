#!/usr/bin/env python3

import pexpect
import json
import platform

# Get tools to run operations on a dev instance via SSH
from dev_instance_tools import (
    reboot_instance,
    wait_until_cmd_done,
    attempt_ssh_connection,
    configure_aws_credentials,
    apply_dpkg_locking_fixup,
    clone_whist_repository_on_instance,
)


def run_host_setup_on_instance(
    pexpect_process, pexpect_prompt, ssh_cmd, timeout_value, logfile, running_in_ci
):
    """
    Run Whist's host setup on a remote machine accessible via a SSH connection within a pexpect process.

    The function assumes that the pexpect_process process has already successfully established a SSH connection to the host, and that the Whist repository has already been cloned.

    Args:
        pexpect_process (pexpect.pty_spawn.spawn): The Pexpect process created with pexpect.spawn(...) and to be used to interact with the remote machine
        pexpect_prompt (str): The bash prompt printed by the shell on the remote machine when it is ready to execute a command
        ssh_cmd (str): The shell command to use to establish a SSH connection to the remote machine. This is used if we need to reboot the machine.
        timeout_value (int): The amount of time to wait before timing out the attemps to gain a SSH connection to the remote machine.
        logfile (file object): The file (already opened) to use for logging the terminal output from the remote machine
        running_in_ci (bool): A boolean indicating whether this script is currently running in CI

    Returns:
        pexpect_process (pexpect.pty_spawn.spawn): The Pexpect process to be used from now on to interact with the remote machine. This is equal to the first argument if a reboot of the remote machine was not needed.
    """

    print("Running the host setup on the instance ...")
    command = "cd ~/whist/host-setup && ./setup_host.sh --localdevelopment | tee ~/host_setup.log"
    pexpect_process.sendline(command)
    result = pexpect_process.expect([pexpect_prompt, "E: Could not get lock"])
    if platform.system() == "Darwin" or result == 1:
        pexpect_process.expect(pexpect_prompt)

    if result == 1:
        # If still getting lock issues, no alternative but to reboot
        print(
            "Running into severe locking issues (happens frequently), rebooting the instance and trying again!"
        )
        pexpect_process = reboot_instance(
            pexpect_process, ssh_cmd, timeout_value, logfile, pexpect_prompt, 5, running_in_ci
        )
        pexpect_process.sendline(command)
        wait_until_cmd_done(pexpect_process, pexpect_prompt, running_in_ci)

    print("Finished running the host setup script on the EC2 instance")
    return pexpect_process


def start_host_service_on_instance(pexpect_process):
    """
    Run Whist's host service on a remote machine accessible via a SSH connection within a pexpect process.

    The function assumes that the pexpect_process process has already successfully established a SSH connection to the host, and that the Whist repository has already been cloned.

    Args:
        pexpect_process (pexpect.pty_spawn.spawn): The Pexpect process created with pexpect.spawn(...) and to be used to interact with the remote machine
    """
    print("Starting the host service on the EC2 instance...")
    command = (
        "sudo rm -rf /whist && cd ~/whist/backend/services/host-service && make run | tee ~/host_service.log"
    )
    pexpect_process.sendline(command)
    pexpect_process.expect("Entering event loop...")
    print("Host service is ready!")


def build_server_on_instance(pexpect_process, pexpect_prompt, cmake_build_type, running_in_ci):
    """
    Build the Whist server (browsers/chrome mandelbox) on a remote machine accessible via a SSH connection within a pexpect process.

    The function assumes that the pexpect_process process has already successfully established a SSH connection to the host, and that the Whist repository has already been cloned.

    Args:
        pexpect_process (pexpect.pty_spawn.spawn): The Pexpect process created with pexpect.spawn(...) and to be used to interact with the remote machine
        pexpect_prompt (str): The bash prompt printed by the shell on the remote machine when it is ready to execute a command
        cmake_build_type (str): A string identifying whether to build the protocol in release, debug, metrics, or any other Cmake build mode that will be introduced later.
        running_in_ci (bool): A boolean indicating whether this script is currently running in CI

    """
    print("Building the server mandelbox in {} mode ...".format(cmake_build_type))
    command = "cd ~/whist/mandelboxes && ./build.sh browsers/chrome --{} | tee ~/server_mandelbox_build.log".format(
        cmake_build_type
    )
    pexpect_process.sendline(command)
    wait_until_cmd_done(pexpect_process, pexpect_prompt, running_in_ci)
    print("Finished building the browsers/chrome (server) mandelbox on the EC2 instance")


def run_server_on_instance(pexpect_process):
    """
    Run the Whist server (browsers/chrome mandelbox) on a remote machine accessible via a SSH connection within a pexpect process.

    The function assumes that the pexpect_process process has already successfully established a SSH connection to the host, that the Whist repository has already been cloned, and that the browsers/chrome mandelbox has already been built. Further, the host service must be already running on the remote machine.

    Args:
        pexpect_process (pexpect.pty_spawn.spawn): The Pexpect process created with pexpect.spawn(...) and to be used to interact with the remote machine

    Return:
        server_docker_id (str): The Docker ID of the container running the Whist server (browsers/chrome mandelbox) on the remote machine
        json_data (dict): A dictionary containing the IP, AES KEY, and port mappings that are needed by the client to successfully connect to the Whist server.
    """
    command = "cd ~/whist/mandelboxes && ./run.sh browsers/chrome | tee ~/server_mandelbox_run.log"
    pexpect_process.sendline(command)
    pexpect_process.expect(":/#")
    server_mandelbox_output = pexpect_process.before.decode("utf-8").strip().split("\n")
    server_docker_id = (
        server_mandelbox_output[-2].replace("\n", "").replace("\r", "").replace(" ", "")
    )
    print("Whist Server started on EC2 instance, on Docker container {}!".format(server_docker_id))

    # Retrieve connection configs from server
    json_data = {}
    for line in server_mandelbox_output:
        if "linux/macos" in line:
            config_vals = line.strip().split()
            server_addr = config_vals[2]
            port_mappings = config_vals[3]
            # Remove leading '-p' chars
            port_mappings = port_mappings[2:].split(".")
            port_32262 = port_mappings[0].split(":")[1]
            port_32263 = port_mappings[1].split(":")[1]
            port_32273 = port_mappings[2].split(":")[1]
            aes_key = config_vals[5]
            json_data["dev_client_server_ip"] = server_addr
            json_data["dev_client_server_port_32262"] = port_32262
            json_data["dev_client_server_port_32263"] = port_32263
            json_data["dev_client_server_port_32273"] = port_32273
            json_data["dev_client_server_aes_key"] = aes_key
    return server_docker_id, json_data


def build_client_on_instance(
    pexpect_process, pexpect_prompt, testing_time, cmake_build_type, running_in_ci
):
    """
    Build the Whist protocol client (development/client mandelbox) on a remote machine accessible via a SSH connection within a pexpect process.

    The function assumes that the pexpect_process process has already successfully established a SSH connection to the host, and that the Whist repository has already been cloned.

    Args:
        pexpect_process (pexpect.pty_spawn.spawn): The Pexpect process created with pexpect.spawn(...) and to be used to interact with the remote machine
        pexpect_prompt (str): The bash prompt printed by the shell on the remote machine when it is ready to execute a command
        testing_time(int): The amount of time to leave the connection open between the client and the server (when the client is started) before shutting it down
        cmake_build_type (str): A string identifying whether to build the protocol in release, debug, metrics, or any other Cmake build mode that will be introduced later.
        running_in_ci (bool): A boolean indicating whether this script is currently running in CI

    """
    # Edit the run-whist-client.sh to make the client quit after the experiment is over
    print("Setting the experiment duration to {}s...".format(testing_time))
    command = "sed -i 's/timeout 240s/timeout {}s/g' ~/whist/mandelboxes/development/client/run-whist-client.sh".format(
        testing_time
    )
    pexpect_process.sendline(command)
    wait_until_cmd_done(pexpect_process, pexpect_prompt, running_in_ci)

    print("Building the dev client mandelbox in {} mode ...".format(cmake_build_type))
    command = "cd ~/whist/mandelboxes && ./build.sh development/client --{} | tee ~/client_mandelbox_build.log".format(
        cmake_build_type
    )
    pexpect_process.sendline(command)
    wait_until_cmd_done(pexpect_process, pexpect_prompt, running_in_ci)
    print("Finished building the dev client mandelbox on the EC2 instance")


def run_client_on_instance(pexpect_process, json_data):
    """
    Run the Whist dev client (development/client mandelbox) on a remote machine accessible via a SSH connection within a pexpect process.

    The function assumes that the pexpect_process process has already successfully established a SSH connection to the host, that the Whist repository has already been cloned, and that the browsers/chrome mandelbox has already been built. Further, the host service must be already running on the remote machine.

    Args:
        pexpect_process (pexpect.pty_spawn.spawn): The Pexpect process created with pexpect.spawn(...) and to be used to interact with the remote machine
        json_data (dict): A dictionary containing the IP, AES KEY, and port mappings that are needed by the client to successfully connect to the Whist server.

    Return:
        client_docker_id (str): The Docker ID of the container running the Whist dev client (development/client mandelbox) on the remote machine

    """
    print("Running the dev client mandelbox, and connecting to the server!")
    command = "cd ~/whist/mandelboxes && ./run.sh development/client --json-data='{}'".format(
        json.dumps(json_data)
    )
    pexpect_process.sendline(command)
    pexpect_process.expect(":/#")
    client_mandelbox_output = pexpect_process.before.decode("utf-8").strip().split("\n")
    client_docker_id = (
        client_mandelbox_output[-2].replace("\n", "").replace("\r", "").replace(" ", "")
    )
    print(
        "Whist dev client started on EC2 instance, on Docker container {}!".format(client_docker_id)
    )
    return client_docker_id


def server_setup_process(args_dict):
    """
    Prepare a remote Ubuntu machine to host the Whist server. This entails:
    - opening a SSH connection to the machine
    - setting up the AWS credentials
    - cloning the current branch of the Whist repository
    - launching the Whist host setup (after having applied a small dpkg-related fixup)
    - building the server mandelbox

    Args:
        args_dict (dict): A dictionary containing the configs needed to access the remote machine and get a Whist server ready for execution

    """
    username = args_dict["username"]
    server_hostname = args_dict["server_hostname"]
    ssh_key_path = args_dict["ssh_key_path"]
    aws_timeout = args_dict["aws_timeout"]
    server_log_filepath = args_dict["server_log_filepath"]
    pexpect_prompt_server = args_dict["pexpect_prompt_server"]
    github_token = args_dict["github_token"]
    aws_credentials_filepath = args_dict["aws_credentials_filepath"]
    cmake_build_type = args_dict["cmake_build_type"]
    running_in_ci = args_dict["running_in_ci"]

    server_log = open(server_log_filepath, "w")

    # Initiate the SSH connections with the instance
    print("Initiating the SETUP ssh connection with the server AWS instance...")
    server_cmd = "ssh {}@{} -i {}".format(username, server_hostname, ssh_key_path)
    hs_process = attempt_ssh_connection(
        server_cmd, aws_timeout, server_log, pexpect_prompt_server, 5
    )
    if not running_in_ci:
        hs_process.expect(pexpect_prompt_server)

    print("Configuring AWS credentials on server instance...")
    configure_aws_credentials(
        hs_process, pexpect_prompt_server, running_in_ci, aws_credentials_filepath
    )

    clone_whist_repository_on_instance(
        github_token, hs_process, pexpect_prompt_server, running_in_ci
    )
    apply_dpkg_locking_fixup(hs_process, pexpect_prompt_server, running_in_ci)

    # 1- run host-setup
    hs_process = run_host_setup_on_instance(
        hs_process, pexpect_prompt_server, server_cmd, aws_timeout, server_log, running_in_ci
    )

    # 2- reboot and wait for it to come back up
    print("Rebooting the server EC2 instance (required after running the host setup)...")
    hs_process = reboot_instance(
        hs_process, server_cmd, aws_timeout, server_log, pexpect_prompt_server, 5, running_in_ci
    )

    # 3- Build the protocol server
    build_server_on_instance(hs_process, pexpect_prompt_server, cmake_build_type, running_in_ci)

    hs_process.kill(0)

    server_log.close()


def client_setup_process(args_dict):
    """
    Prepare a remote Ubuntu machine to host the Whist dev client. This entails:
    - opening a SSH connection to the machine
    - if the client is not running on the same machine as the server:
        - setting up the AWS credentials
        - cloning the current branch of the Whist repository
        - launching the Whist host setup (after having applied a small dpkg-related fixup)
    - building the client mandelbox

    Args:
        args_dict (dict): A dictionary containing the configs needed to access the remote machine and get a Whist dev client ready for execution

    """
    username = args_dict["username"]
    client_hostname = args_dict["client_hostname"]
    ssh_key_path = args_dict["ssh_key_path"]
    aws_timeout = args_dict["aws_timeout"]
    client_log_filepath = args_dict["client_log_filepath"]
    pexpect_prompt_client = args_dict["pexpect_prompt_client"]
    github_token = args_dict["github_token"]
    use_two_instances = args_dict["use_two_instances"]
    testing_time = args_dict["testing_time"]
    aws_credentials_filepath = args_dict["aws_credentials_filepath"]
    cmake_build_type = args_dict["cmake_build_type"]
    running_in_ci = args_dict["running_in_ci"]

    client_log = open(client_log_filepath, "w")

    client_cmd = "ssh {}@{} -i {}".format(username, client_hostname, ssh_key_path)

    if use_two_instances:
        # Initiate the SSH connections with the client instance
        print("Initiating the SETUP ssh connection with the client AWS instance...")
        hs_process = attempt_ssh_connection(
            client_cmd, aws_timeout, client_log, pexpect_prompt_client, 5
        )
        if not running_in_ci:
            hs_process.expect(pexpect_prompt_client)
        print("Configuring AWS credentials on client instance...")
        configure_aws_credentials(
            hs_process, pexpect_prompt_client, running_in_ci, aws_credentials_filepath
        )

        clone_whist_repository_on_instance(
            github_token, hs_process, pexpect_prompt_client, running_in_ci
        )
        apply_dpkg_locking_fixup(hs_process, pexpect_prompt_client, running_in_ci)

        # 1- run host-setup
        hs_process = run_host_setup_on_instance(
            hs_process, pexpect_prompt_client, client_cmd, aws_timeout, client_log, running_in_ci
        )

        # 2- reboot and wait for it to come back up
        print("Rebooting the server EC2 instance (required after running the host setup)...")
        hs_process = reboot_instance(
            hs_process, client_cmd, aws_timeout, client_log, pexpect_prompt_client, 5, running_in_ci
        )

        hs_process.kill(0)

    # 6- Build the dev client
    print("Initiating the BUILD ssh connection with the client AWS instance...")
    client_pexpect_process = attempt_ssh_connection(
        client_cmd, aws_timeout, client_log, pexpect_prompt_client, 5
    )
    build_client_on_instance(
        client_pexpect_process, pexpect_prompt_client, testing_time, cmake_build_type, running_in_ci
    )
    client_pexpect_process.kill(0)

    client_log.close()
