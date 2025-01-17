#!/usr/bin/env python3

import pexpect
import os
import sys

from e2e_helpers.common.git_tools import (
    get_whist_branch_name,
)

from e2e_helpers.common.pexpect_tools import (
    expression_in_pexpect_output,
    wait_until_cmd_done,
    get_command_exit_code,
)

from e2e_helpers.common.ssh_tools import (
    wait_for_apt_locks,
)

from e2e_helpers.common.timestamps_and_exit_tools import (
    exit_with_error,
    printformat,
)

from e2e_helpers.common.constants import (
    SETUP_MAX_RETRIES,
    HOST_SETUP_TIMEOUT_SECONDS,
    TIMEOUT_EXIT_CODE,
    TIMEOUT_KILL_EXIT_CODE,
    aws_credentials_filepath,
    running_in_ci,
)

# Add the current directory to the path no matter where this is called from
sys.path.append(os.path.join(os.getcwd(), os.path.dirname(__file__), "."))


def prepare_instance_for_host_setup(pexpect_process, pexpect_prompt):
    # Set dkpg frontend as non-interactive to avoid irrelevant warnings
    pexpect_process.sendline(
        "echo 'debconf debconf/frontend select Noninteractive' | sudo debconf-set-selections"
    )
    wait_until_cmd_done(pexpect_process, pexpect_prompt)

    # Wait for dpkg / apt locks
    wait_for_apt_locks(pexpect_process, pexpect_prompt)

    # Remove the Ubuntu welcome message, to reduce noise in the logs
    # https://askubuntu.com/questions/676374/how-to-disable-welcome-message-after-ssh-login
    pexpect_process.sendline("sudo chmod -x /etc/update-motd.d/*")
    wait_until_cmd_done(pexpect_process, pexpect_prompt)

    # Clean, upgrade and update all the apt lists
    pexpect_process.sendline(
        "sudo apt-get -y -q clean && sudo apt-get -y -q upgrade && sudo apt-get -y -q update"
    )
    wait_until_cmd_done(pexpect_process, pexpect_prompt)


def get_aws_credentials():
    """
    Obtain the AWS credentials that are currently in use on the machine where this script is being run.
    Args:
        None
    Returns:
        aws_access_key_id (str): The AWS access key ID
        aws_secret_access_key (str): The AWS secret access key
    """
    # Step 1: Obtain AWS credentials from the local machine
    aws_access_key_id = ""
    aws_secret_access_key = ""
    if running_in_ci:
        # In CI, the AWS credentials are stored in the following env variables
        print("Getting the AWS credentials from environment variables...")
        aws_access_key_id = os.getenv("AWS_ACCESS_KEY_ID")
        aws_secret_access_key = os.getenv("AWS_SECRET_ACCESS_KEY")
    else:
        # Extract AWS credentials from aws configuration file on disk
        if not os.path.isfile(aws_credentials_filepath):
            exit_with_error(
                f"Could not find local AWS credential file at path {aws_credentials_filepath}!"
            )
        aws_credentials_file = open(aws_credentials_filepath, "r")

        # Read the AWS configuration file
        for line in aws_credentials_file.readlines():
            if "aws_access_key_id" in line:
                aws_access_key_id = line.strip().split()[2]
            elif "aws_secret_access_key" in line:
                aws_secret_access_key = line.strip().split()[2]
                break
        aws_credentials_file.close()

    # Sanity check
    if len(aws_access_key_id or "") == 0 or len(aws_secret_access_key or "") == 0:
        exit_with_error(f"Could not obtain the AWS credentials!")

    return aws_access_key_id, aws_secret_access_key


def install_and_configure_aws(
    pexpect_process,
    pexpect_prompt,
):
    """
    Install the AWS CLI and configure the AWS credentials on a remote machine by copying them
    from the ones configured on the machine where this script is being run.

    Args:
        pexpect_process (pexpect.pty_spawn.spawn):  The Pexpect process created with pexpect.spawn(...) and to be used
                                                    to interact with the remote machine
        pexpect_prompt: The bash prompt printed by the shell on the remote machine when it
                        is ready to execute a command

    Returns:
        True if the AWS installation and configuration succeeded, False otherwise.
    """
    # Step 1: Obtain AWS credentials from the local machine
    aws_access_key_id, aws_secret_access_key = get_aws_credentials()

    # Wait for apt locks
    wait_for_apt_locks(pexpect_process, pexpect_prompt)

    # Step 2: Install the AWS CLI if it's not already there
    pexpect_process.sendline("sudo apt-get -y -q update")
    wait_until_cmd_done(pexpect_process, pexpect_prompt)
    # Check if the AWS CLI is installed, and install it if not.
    pexpect_process.sendline("aws --version")
    stdout = wait_until_cmd_done(
        pexpect_process,
        pexpect_prompt,
        return_output=True,
    )
    # Check if the message below, indicating that aws is not installed, is present in the output.
    error_msg = "Command 'aws' not found, but can be installed with:"
    # Attempt installation using apt-get
    if expression_in_pexpect_output(error_msg, stdout):
        print("Installing AWS-CLI using apt-get")

        # Wait for apt locks
        wait_for_apt_locks(pexpect_process, pexpect_prompt)

        pexpect_process.sendline("sudo apt-get install -y awscli")
        stdout = wait_until_cmd_done(
            pexpect_process,
            pexpect_prompt,
            return_output=True,
        )

        # Check if the apt-get installation failed (it happens from time to time)
        error_msg = "E: Package 'awscli' has no installation candidate"
        if expression_in_pexpect_output(error_msg, stdout):
            exit_with_error(
                "Installing AWS-CLI using apt-get failed. This usually happens when the Ubuntu package lists are being updated."
            )
    else:
        print("AWS CLI is already installed")

    # Step 3: Set the AWS credentials
    access_key_cmd = f"aws configure set aws_access_key_id {aws_access_key_id}"
    pexpect_process.sendline(access_key_cmd)
    wait_until_cmd_done(pexpect_process, pexpect_prompt)
    secret_access_key_cmd = f"aws configure set aws_secret_access_key {aws_secret_access_key}"
    pexpect_process.sendline(secret_access_key_cmd)
    wait_until_cmd_done(pexpect_process, pexpect_prompt)

    print("AWS configuration is now complete!")


def clone_whist_repository(github_token, pexpect_process, pexpect_prompt):
    """
    Clone the Whist repository on a remote machine, and check out the same branch used locally
    on the machine where this script is run.

    Args:
        github_token:   The secret token to use to access the Whist private repository on GitHub
        pexpect_process (pexpect.pty_spawn.spawn):  The Pexpect process created with pexpect.spawn(...) and to be used to
                                                    interact with the remote machine
        pexpect_prompt: The bash prompt printed by the shell on the remote machine when it is
                        ready to execute a command

    Returns:
        None
    """
    branch_name = get_whist_branch_name()
    git_clone_exit_code = 1

    for retry in range(SETUP_MAX_RETRIES):
        print(
            f"Cloning branch {branch_name} of the whisthq/whist repository on the AWS instance (retry {retry+1}/{SETUP_MAX_RETRIES})..."
        )
        # Retrieve whisthq/whist monorepo on the instance
        command = (
            "rm -rf whist; git clone --quiet -b "
            + branch_name
            + " https://"
            + github_token
            + "@github.com/whisthq/whist.git | tee ~/github_log.log"
        )
        pexpect_process.sendline(command)
        git_clone_stdout = wait_until_cmd_done(pexpect_process, pexpect_prompt, return_output=True)
        git_clone_exit_code = get_command_exit_code(pexpect_process, pexpect_prompt)
        branch_not_found_error = f"fatal: Remote branch {branch_name} not found in upstream origin"
        if git_clone_exit_code != 0 or expression_in_pexpect_output("fatal: ", git_clone_stdout):
            if expression_in_pexpect_output(branch_not_found_error, git_clone_stdout):
                # If branch does not exist, trigger fatal error.
                exit_with_error(
                    f"Branch {branch_name} not found in the whisthq/whist repository. Maybe it has been merged while the E2E was running?"
                )
            else:
                printformat(f"Git clone failed!", "yellow")
        else:
            break

    if git_clone_exit_code != 0:
        exit_with_error(f"git clone failed {SETUP_MAX_RETRIES}! Giving up now.")

    print("Finished downloading whisthq/whist on EC2 instance")


def run_host_setup(
    pexpect_process,
    pexpect_prompt,
):
    """
    Run Whist's host setup on a remote machine accessible via a SSH connection within a pexpect process.

    The function assumes that the pexpect_process process has already successfully established a SSH
    connection to the host, and that the Whist repository has already been cloned.

    Args:
        pexpect_process (pexpect.pty_spawn.spawn):  The Pexpect process created with pexpect.spawn(...) and to
                                                    be used to interact with the remote machine
        pexpect_prompt (str):   The bash prompt printed by the shell on the remote machine when it is ready to
                                execute a command

    Returns:
        None
    """

    success_msg = "Install complete. If you set this machine up for local development, please 'sudo reboot' before continuing."
    lock_error_msg = "E: Could not get lock"
    dpkg_config_error = "E: dpkg was interrupted, you must manually run 'sudo dpkg --configure -a' to correct the problem."
    command = f"cd ~/whist/host-setup && timeout {HOST_SETUP_TIMEOUT_SECONDS} ./setup_host.sh --localdevelopment | tee ~/host_setup.log"

    for retry in range(SETUP_MAX_RETRIES):
        print(f"Running the host setup on the instance (retry {retry+1}/{SETUP_MAX_RETRIES})...")
        # 1- Ensure that the apt/dpkg locks are not taken by other processes
        wait_for_apt_locks(pexpect_process, pexpect_prompt)

        # 2 - Run the host setup command and grab the output
        pexpect_process.sendline(command)
        host_setup_output = wait_until_cmd_done(pexpect_process, pexpect_prompt, return_output=True)
        host_setup_exit_code = get_command_exit_code(pexpect_process, pexpect_prompt)

        # 3 - Check if the setup succeeded or report reason for failure
        if (
            expression_in_pexpect_output(success_msg, host_setup_output)
            or host_setup_exit_code == 0
        ):
            print("Finished running the host setup script on the EC2 instance")
            break
        elif expression_in_pexpect_output(lock_error_msg, host_setup_output):
            printformat("Host setup failed to grab the necessary apt/dpkg locks.", "yellow")
        elif expression_in_pexpect_output(dpkg_config_error, host_setup_output):
            printformat(
                "Host setup failed due to dpkg interruption error. Reconfiguring dpkg....", "yellow"
            )
            pexpect_process.sendline("sudo dpkg --force-confdef --configure -a ")
            wait_until_cmd_done(pexpect_process, pexpect_prompt)
        elif (
            host_setup_exit_code == TIMEOUT_EXIT_CODE
            or host_setup_exit_code == TIMEOUT_KILL_EXIT_CODE
        ):
            printformat(f"Host setup timed out after {HOST_SETUP_TIMEOUT_SECONDS}s!", "yellow")
        else:
            printformat("Host setup failed for unspecified reason (check the logs)!", "yellow")

        if retry == SETUP_MAX_RETRIES - 1:
            exit_with_error(f"Host setup failed {SETUP_MAX_RETRIES} times. Giving up now!")


def start_host_service(pexpect_process, pexpect_prompt):
    """
    Run Whist's host service on a remote machine accessible via a SSH connection within a pexpect process.

    The function assumes that the pexpect_process process has already successfully established a SSH connection
    to the host, and that the Whist repository has already been cloned.

    Args:
        pexpect_process (pexpect.pty_spawn.spawn):  The Pexpect process created with pexpect.spawn(...) and to be used to
                                                    interact with the remote machine

    Returns:
        None
    """
    print("Starting the host service on the EC2 instance...")
    command = "sudo rm -rf /whist && cd ~/whist/backend/services && make run_host_service | tee ~/host_service.log"
    pexpect_process.sendline(command)

    desired_output = "Entering event loop..."

    result = pexpect_process.expect(
        [desired_output, pexpect_prompt, pexpect.exceptions.TIMEOUT, pexpect.EOF]
    )

    # If the desired output does not get printed, handle potential host service startup issues.
    if result != 0:
        exit_with_error("Host service failed to start! Check the logs for troubleshooting!")

    print("Host service is ready!")


def prune_containers_if_needed(pexpect_process, pexpect_prompt):
    """
    Check whether the remote instance is running out of space (more specifically,
    check if >= 75 % of disk space is used). If the disk is getting full, free some space
    by pruning old Docker containers by running the command `docker system prune -af`

    Args:
        pexpect_process (pexpect.pty_spawn.spawn):  The Pexpect process created with pexpect.spawn(...) and to be used
                                                    to interact with the remote machine
        pexpect_prompt (str):   The bash prompt printed by the shell on the remote machine when it is
                                ready to execute a command

    Returns:
        None
    """
    # Check if we are running out of space
    pexpect_process.sendline("df -h | grep --color=never /dev/root")
    space_used_output = wait_until_cmd_done(pexpect_process, pexpect_prompt, return_output=True)
    for line in reversed(space_used_output):
        if "/dev/root" in line:
            space_used_output = line.split()
            break
    space_used_pctg = int(space_used_output[-2][:-1])

    # Clean up space on the instance by pruning all Docker containers if the disk is 75% (or more) full
    if space_used_pctg >= 75:
        print(f"Disk is {space_used_pctg}% full, pruning the docker containers...")
        pexpect_process.sendline("docker system prune -af")
        wait_until_cmd_done(pexpect_process, pexpect_prompt)
    else:
        print(f"Disk is {space_used_pctg}% full, no need to prune containers.")
