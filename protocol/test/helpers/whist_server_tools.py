#!/usr/bin/env python3

import os, sys

from helpers.common.pexpect_tools import (
    expression_in_pexpect_output,
    wait_until_cmd_done,
    get_command_exit_code,
)

from helpers.common.ssh_tools import (
    attempt_ssh_connection,
    reboot_instance,
)

from helpers.setup.instance_setup_tools import (
    install_and_configure_aws,
    clone_whist_repository,
    run_host_setup,
    prune_containers_if_needed,
    prepare_instance_for_host_setup,
)

from helpers.common.git_tools import (
    get_remote_whist_github_sha,
    get_whist_github_sha,
)
from helpers.common.timestamps_and_exit_tools import (
    printformat,
    exit_with_error,
)

from helpers.common.constants import (
    MANDELBOX_BUILD_MAX_RETRIES,
    username,
)

# Add the current directory to the path no matter where this is called from
sys.path.append(os.path.join(os.getcwd(), os.path.dirname(__file__), "."))


def server_setup_process(args_dict):
    """
    Prepare a remote Ubuntu machine to host the Whist server. This entails:
    - opening a SSH connection to the machine
    - installing the AWS CLI and setting up the AWS credentials
    - pruning the docker containers if we are running out of space on disk
    - cloning the current branch of the Whist repository if needed
    - launching the Whist host setup if needed
    - building the server mandelbox

    In case of error, this function makes the process running it exit with exitcode -1.

    Args:
        args_dict (multiprocessing.managers.DictProxy): A dictionary containing the configs needed to access the remote
                                                        machine and get a Whist server ready for execution

    Returns:
        None
    """
    server_hostname = args_dict["server_hostname"]
    ssh_key_path = args_dict["ssh_key_path"]
    server_log_filepath = args_dict["server_log_filepath"]
    pexpect_prompt_server = args_dict["pexpect_prompt_server"]
    github_token = args_dict["github_token"]
    cmake_build_type = args_dict["cmake_build_type"]
    skip_git_clone = args_dict["skip_git_clone"]
    skip_host_setup = args_dict["skip_host_setup"]

    server_log = open(server_log_filepath, "w")

    # Initiate the SSH connections with the instance
    print("Initiating the SETUP ssh connection with the server AWS instance...")
    server_cmd = f"ssh {username}@{server_hostname} -i {ssh_key_path}"
    hs_process = attempt_ssh_connection(
        server_cmd,
        server_log,
        pexpect_prompt_server,
    )

    print("Running pre-host-setup on the instance...")
    prepare_instance_for_host_setup(hs_process, pexpect_prompt_server)

    print("Configuring AWS credentials on server instance...")
    install_and_configure_aws(
        hs_process,
        pexpect_prompt_server,
    )

    prune_containers_if_needed(hs_process, pexpect_prompt_server)

    if skip_git_clone == "false":
        clone_whist_repository(github_token, hs_process, pexpect_prompt_server)
    else:
        print("Skipping git clone whisthq/whist repository on server instance.")

    # Ensure that the commit hash on server matches the one on the runner
    server_sha = get_remote_whist_github_sha(hs_process, pexpect_prompt_server)
    local_sha = get_whist_github_sha()
    if server_sha != local_sha:
        exit_with_error(
            f"Commit mismatch between server instance ({server_sha}) and E2E runner ({local_sha}). This can happen when re-running a CI workflow after having pushed new commits."
        )

    if skip_host_setup == "false":
        run_host_setup(
            hs_process,
            pexpect_prompt_server,
        )
    else:
        print("Skipping host setup on server instance.")

    # Reboot and wait for it to come back up
    print("Rebooting the server EC2 instance (required after running the host-setup)...")
    hs_process = reboot_instance(
        hs_process,
        server_cmd,
        server_log,
        pexpect_prompt_server,
    )

    # Build the protocol server
    build_server_on_instance(hs_process, pexpect_prompt_server, cmake_build_type)

    hs_process.kill(0)
    server_log.close()


def build_server_on_instance(pexpect_process, pexpect_prompt, cmake_build_type):
    """
    Build the Whist server (browsers/chrome mandelbox) on a remote machine accessible via a SSH
    connection within a pexpect process.

    The function assumes that the pexpect_process process has already successfully established a
    SSH connection to the host, and that the Whist repository has already been cloned.

    Args:
        pexpect_process (pexpect.pty_spawn.spawn):  The Pexpect process created with pexpect.spawn(...) and to be used to
                                                    interact with the remote machine
        pexpect_prompt (str):   The bash prompt printed by the shell on the remote machine when
                                it is ready to execute a command
        cmake_build_type (str): A string identifying whether to build the protocol in release,
                                debug, metrics, or any other Cmake build mode that will be introduced later.

    Returns:
        None
    """

    command = f"cd ~/whist/mandelboxes && ./build.sh browsers/chrome --{cmake_build_type} | tee ~/server_mandelbox_build.log"
    success_msg = "All images built successfully!"
    docker_tar_io_eof_error = "io: read/write on closed pipe"
    docker_connect_error = "error during connect: Post"

    for retry in range(MANDELBOX_BUILD_MAX_RETRIES):
        print(
            f"Building the server mandelbox in {cmake_build_type} mode (retry {retry+1}/{MANDELBOX_BUILD_MAX_RETRIES})..."
        )

        # Attempt to build the browsers/chrome mandelbox and grab the output + the exit code
        pexpect_process.sendline(command)
        build_server_output = wait_until_cmd_done(
            pexpect_process, pexpect_prompt, return_output=True
        )
        build_server_exit_code = get_command_exit_code(pexpect_process, pexpect_prompt)

        # Check if the build succeeded
        if build_server_exit_code == 0 and expression_in_pexpect_output(
            success_msg, build_server_output
        ):
            print("Finished building the browsers/chrome (server) mandelbox on the EC2 instance")
            break
        else:
            printformat(
                "Could not build the browsers/chrome mandelbox on the server instance!", "yellow"
            )
            if expression_in_pexpect_output(
                docker_tar_io_eof_error, build_client_output
            ) or expression_in_pexpect_output(docker_connect_error, build_client_output):
                print("Detected docker build issue. Attempting to fix by restarting docker!")
                pexpect_process.sendline("sudo service docker restart")
                wait_until_cmd_done(pexpect_process, pexpect_prompt)
        if retry == MANDELBOX_BUILD_MAX_RETRIES - 1:
            # If building the browsers/chrome mandelbox fails too many times, trigger a fatal error.
            exit_with_error(
                f"Building the browsers/chrome mandelbox failed {MANDELBOX_BUILD_MAX_RETRIES} times. Giving up now!"
            )


def run_server_on_instance(pexpect_process):
    """
    Run the Whist server (browsers/chrome mandelbox) on a remote machine accessible via a SSH
    connection within a pexpect process.

    The function assumes that the pexpect_process process has already successfully established
    a SSH connection to the host, that the Whist repository has already been cloned, and that
    the browsers/chrome mandelbox has already been built. Further, the host service must be
    already running on the remote machine.

    Args:
        pexpect_process (pexpect.pty_spawn.spawn):  The Pexpect process created with pexpect.spawn(...) and to be used to
                                                    interact with the remote machine

    Returns:
        server_docker_id (str): The Docker ID of the container running the Whist server
                                (browsers/chrome mandelbox) on the remote machine
        json_data (str):    A dictionary containing the IP, AES KEY, and port mappings that are needed by
                            the client to successfully connect to the Whist server.
    """
    command = "cd ~/whist/mandelboxes && ./run.sh browsers/chrome | tee ~/server_mandelbox_run.log"
    pexpect_process.sendline(command)
    # Need to wait for special mandelbox prompt ":/#". prompt_printed_twice must always be set to False in this case.
    server_mandelbox_output = wait_until_cmd_done(
        pexpect_process, ":/#", prompt_printed_twice=False, return_output=True
    )
    server_docker_id = server_mandelbox_output[-2].replace(" ", "")
    print(f"Whist Server started on EC2 instance, on Docker container {server_docker_id}!")

    # Retrieve connection configs from server
    json_data = {}
    for line in server_mandelbox_output:
        if "linux/macos" in line:
            config_vals = line.strip().split()
            server_addr = config_vals[2]
            port_mappings = config_vals[3]
            aes_key = config_vals[5]
            json_data["dev_client_server_ip"] = server_addr
            # Remove leading '-p' chars
            json_data["dev_client_server_port_mappings"] = port_mappings[2:]
            json_data["dev_client_server_aes_key"] = aes_key
    return server_docker_id, json_data


def shutdown_and_wait_server_exit(pexpect_process, session_id, exit_confirm_exp):
    """
    Initiate shutdown and wait for server exit to see if the server hangs or exits gracefully

    Args:
        pexpect_process (pexpect.pty_spawn.spawn):  Server pexpect process - MUST BE AFTER DOCKER COMMAND WAS RUN - otherwise
                                                    behavior is undefined
        session_id (str): The protocol session id (if set), or an empty string (otherwise)
        exit_confirm_exp (str): Target expression to expect on a graceful server exit

    Returns:
        server_has_exited (bool): A boolean set to True if server has exited gracefully, false otherwise
    """
    # Shut down Chrome
    pexpect_process.sendline("pkill chrome")

    # We set prompt_printed_twice=False because the Docker bash does not print in color
    # (check wait_until_cmd_done docstring for more details about handling color bash stdout)
    wait_until_cmd_done(pexpect_process, ":/#", prompt_printed_twice=False)

    # Give WhistServer 20s to shutdown properly
    pexpect_process.sendline("sleep 20")
    wait_until_cmd_done(pexpect_process, ":/#", prompt_printed_twice=False)

    # Check the log to see if WhistServer shut down gracefully or if there was a server hang
    server_log_filepath = os.path.join("/var/log/whist", session_id, "protocol-out.log")
    pexpect_process.sendline(f"tail {server_log_filepath}")
    server_mandelbox_output = wait_until_cmd_done(
        pexpect_process, ":/#", prompt_printed_twice=False, return_output=True
    )
    server_has_exited = expression_in_pexpect_output(exit_confirm_exp, server_mandelbox_output)

    # Kill tail process
    pexpect_process.sendcontrol("c")
    return server_has_exited
