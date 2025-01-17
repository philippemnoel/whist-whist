#!/usr/bin/env python3

import os, sys, subprocess

from github import Github

from e2e_helpers.common.pexpect_tools import wait_until_cmd_done
from e2e_helpers.common.timestamps_and_exit_tools import printformat, exit_with_error
from e2e_helpers.common.constants import GITHUB_SHA_LEN, running_in_ci

# Add the current directory to the path no matter where this is called from
sys.path.append(os.path.join(os.getcwd(), os.path.dirname(__file__), "."))


def get_whist_branch_name():
    """
    Retrieve the whisthq/whist branch name currently checked out by the folder containing this script.

    Args:
        None

    Returns:
        On success:
            branch_name (str): The name of the branch currently checked out (or the empty string)
        On failure:
            Empty string (str)
    """
    branch_name = ""

    if running_in_ci:
        # In CI, the PR branch name is saved in GITHUB_REF_NAME, or in the GITHUB_HEAD_REF environment
        # variable (in case this script is being run as part of a PR)
        b = os.getenv("GITHUB_REF_NAME").split("/")
        if len(b) != 2 or not b[0].isnumeric() or b[1] != "merge":
            branch_name = os.getenv("GITHUB_REF_NAME")
        else:
            branch_name = os.getenv("GITHUB_HEAD_REF")
    else:
        # Locally, we can find the branch using the 'git branch' command.
        # WARNING: this command will fail on detached HEADS.
        subproc_handle = subprocess.Popen("git branch", shell=True, stdout=subprocess.PIPE)
        subprocess_stdout = subproc_handle.stdout.readlines()

        for line in subprocess_stdout:
            converted_line = line.decode("utf-8").strip()
            if "*" in converted_line:
                branch_name = converted_line[2:]
                break

    return branch_name


def get_whist_github_sha():
    """
    Retrieve the commit hash of the latest commit in the local repository.

    Args:
        None

    Returns:
        On success:
            github_sha (str): The SHA commit hash
        On failure:
            empty string (str)
    """
    github_sha = ""

    if running_in_ci:
        # In CI, we save the commit hash in the variable GITHUB_COMMIT_HASH
        github_sha = os.getenv("GITHUB_COMMIT_HASH")
    else:
        # Locally, we can find the branch using the 'git rev-parse HEAD' command.
        subproc_handle = subprocess.Popen("git rev-parse HEAD", shell=True, stdout=subprocess.PIPE)
        subprocess_stdout = subproc_handle.stdout.readlines()
        converted_line = subprocess_stdout[0].decode("utf-8").strip()
        if len(converted_line) == GITHUB_SHA_LEN:
            github_sha = converted_line

    return github_sha


def get_remote_whist_github_sha(pexpect_process, pexpect_prompt):
    """
    Retrieve the commit hash of the latest commit in the repository cloned on a remote isntance.

    Args:
        pexpect_process (pexpect.pty_spawn.spawn):  The Pexpect process created with pexpect.spawn(...) and to
                                                    be used to interact with the remote machine
        pexpect_prompt: The bash prompt printed by the shell on the remote machine when it
                        is ready to execute a command

    Returns:
        On success:
            github_sha (str): The SHA commit hash
        On failure:
            empty string (str)
    """

    pexpect_process.sendline("cd ~/whist && git rev-parse HEAD")
    stdout = wait_until_cmd_done(pexpect_process, pexpect_prompt, return_output=True)
    stdout_expected_len = (
        2 if running_in_ci else 3
    )  # if running outside of CI, additional formatting characters will be added
    if len(stdout) != stdout_expected_len or len(stdout[1]) != GITHUB_SHA_LEN:
        print(stdout)
        printformat(
            "Could not get the Github SHA of the commit checked out on the remote instance", "red"
        )
        return ""
    return stdout[1]


def get_workflow_handle():
    """
    Get the github workflow object corresponding to the workflow running this script

    Args:
        None

    Returns:
        On success:
            workflow_handle (github.Workflow.Workflow): The handle to the Github workflow running this script
        On failure:
            None
    """
    git_token = os.getenv("GITHUB_TOKEN") or ""
    if len(git_token) != 40:
        print("Either the GITHUB_TOKEN env is not set, or a token of the wrong length was passed.")
        return None
    workflow_name = os.getenv("GITHUB_WORKFLOW") or ""
    if len(workflow_name) == 0:
        print("Could not get the workflow name!")
        return None
    git_client = Github(git_token)
    if not git_client:
        print("Could not obtain Github client!")
        return None
    repo = git_client.get_repo("whisthq/whist")
    if not repo:
        print("Could not access the whisthq/whist repository!")
        return None

    workflows = [w for w in repo.get_workflows() if w.name == workflow_name]
    if len(workflows) != 1:
        print(f"Could not access the `{workflow_name}` workflow!")
        return None

    return workflows[0]


def count_runs_to_prioritize(workflow, raw_github_run_id):
    """
    Get the number of runs that will need to complete before the current script can access the AWS shared instances.
    This function assumes that the arguments are not set to None and are valid.

    Args:
        workflow (github.Workflow.Workflow): The handle to the workflow running this script
        raw_github_run_id (str): The current github run ID.

    Returns:
        number_of_runs (int): The number of runs that we have to give priority to
    """
    # Sanity check the parameters. raw_github_run_id should be passed as a string, not integer
    if not workflow or len(raw_github_run_id) == 0:
        exit_with_error("Either the workflow handle or the github run id is invalid")
    github_run_id = int(raw_github_run_id)
    # possible github statuses are: "queued", "in_progress", "completed"
    running_states = ["queued", "in_progress"]
    prioritized_runs = [
        run for status in running_states for run in workflow.get_runs(status=status)
    ]
    this_run_start_time = None
    found = False
    for w in prioritized_runs:
        if w.id == github_run_id:
            this_run_start_time = w.run_started_at
            found = True
            prioritized_runs = [
                r_priority
                for r_priority in prioritized_runs
                if r_priority.id != github_run_id
                and (
                    (r_priority.run_started_at < this_run_start_time)
                    or (
                        r_priority.run_started_at <= this_run_start_time
                        and r_priority.id < github_run_id
                    )
                )
            ]
            break
    assert found == True
    return len(prioritized_runs)
