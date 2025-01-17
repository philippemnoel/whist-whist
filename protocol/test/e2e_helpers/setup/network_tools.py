#!/usr/bin/env python3

import os, sys

from e2e_helpers.common.pexpect_tools import (
    expression_in_pexpect_output,
    wait_until_cmd_done,
)

from e2e_helpers.common.ssh_tools import (
    wait_for_apt_locks,
)

from e2e_helpers.common.constants import (
    N_NETWORK_CONDITION_PARAMETERS,
)

# Add the current directory to the path no matter where this is called from
sys.path.append(os.path.join(os.getcwd(), os.path.dirname(__file__), "."))


def setup_artificial_network_conditions(
    pexpect_process, pexpect_prompt, network_conditions, testing_time
):
    """
    Set up the network degradation conditions on the client instance. We apply the network settings
    on the client side to simulate the condition where the user is using Whist on a machine that
    is connected to the Internet through an unstable connection.

    The function assumes that the pexpect_process process has already successfully established a
    SSH connection to the host.

    Args:
        pexpect_process (pexpect.pty_spawn.spawn):  The Pexpect process created with pexpect.spawn(...) and to
                                                    be used to interact with the remote machine
        pexpect_prompt (str):   The bash prompt printed by the shell on the remote machine when it is
                                ready to execute a command
        network_conditions (str):   The network conditions expressed as either 'normal' for no network
                                    degradation or max_bandwidth, delay, packet drop percentage, with the
                                    three values separated by commas and no space.
        testing_time (int): The duration of the E2E test in seconds

    Returns:
        None
    """
    if network_conditions == "normal":
        print("Setting up client to run on a instance with no degradation on network conditions")
    else:
        # Apply conditions below only for values that are actually set
        if len(network_conditions.split(",")) != N_NETWORK_CONDITION_PARAMETERS:
            print(
                "Network conditions passed in incorrect format. Setting up client to run on a instance with no degradation on network conditions"
            )
            return

        bandwidth, delay, pkt_drop_pctg, limit, interval = network_conditions.split(",")
        if bandwidth == "None" and delay == "None" and pkt_drop_pctg == "None":
            print(
                "Setting up client to run on a instance with no degradation on network conditions"
            )
            return

        def parse_value_or_range(raw_string, condition_name, degradation_command_flag, unit=""):
            degradations_command_entry = ""
            if raw_string != "None":
                raw_string = raw_string.split("-")
                if len(raw_string) == 1:
                    min_value = max_value = raw_string[0]
                    degradations_command_entry = f" {degradation_command_flag} {min_value}"
                    print(f"\t* {condition_name}: stable at {min_value}{unit}")
                elif len(raw_string) == 2:
                    min_value, max_value = raw_string
                    degradations_command_entry = (
                        f" {degradation_command_flag} {min_value},{max_value}"
                    )
                    print(
                        f"\t* {condition_name}: variable between {min_value}{unit} and {max_value}{unit}"
                    )
                else:
                    print(
                        f"Error, incorrect number of values passed to {condition_name} network condition"
                    )
            return degradations_command_entry

        degradations_command = ""
        print("Setting up client to run on a instance with the following networking conditions:")
        degradations_command += parse_value_or_range(bandwidth, "max bandwidth", "-b")
        degradations_command += parse_value_or_range(delay, "packet delay", "-q", "ms")
        degradations_command += parse_value_or_range(pkt_drop_pctg, "packet drop rate", "-p", "%")
        degradations_command += parse_value_or_range(
            limit, "maximum number of packets the qdisc may hold queued at a time", "-l"
        )
        degradations_command += parse_value_or_range(
            interval, "net conditions change interval", "-i", "ms"
        )

        # Wait for apt lock
        wait_for_apt_locks(pexpect_process, pexpect_prompt)

        # Install ifconfig
        command = "sudo apt-get install -y net-tools"
        pexpect_process.sendline(command)
        wait_until_cmd_done(pexpect_process, pexpect_prompt)

        # Get network interface names (excluding loopback)
        command = "sudo ifconfig -a | sed 's/[ ].*//;/^\(lo:\|\)$/d'"
        pexpect_process.sendline(command)

        ifconfig_output = wait_until_cmd_done(pexpect_process, pexpect_prompt, return_output=True)

        blacklisted_expressions = [
            pexpect_prompt,
            "docker",
            "veth",
            "ifb",
            "ifconfig",
            "\\",
            "~",
            ";",
        ]
        network_devices = [
            x.replace(":", "")
            for x in ifconfig_output
            if not any(
                blacklisted_expression in x for blacklisted_expression in blacklisted_expressions
            )
        ]
        # add 2 more seconds to testing_time to account for time spent in this script before test actually starts
        command = f"( nohup ~/whist/protocol/test/helpers/setup/apply_network_conditions.sh -d {','.join(network_devices)} -t {testing_time+2} {degradations_command} > ~/network_conditions.log 2>&1 & )"
        pexpect_process.sendline(command)
        wait_until_cmd_done(pexpect_process, pexpect_prompt)


def restore_network_conditions(pexpect_process, pexpect_prompt):
    """
    Restore network conditions to factory ones. This function is idempotent and is safe to run even
    if no network degradation was initially applied.

    The function assumes that the pexpect_process process has already successfully established a
    SSH connection to the host

    Args:
        pexpect_process (pexpect.pty_spawn.spawn):  The Pexpect process created with pexpect.spawn(...) and to be used to
                                                    interact with the remote machine
        pexpect_prompt (str):   The bash prompt printed by the shell on the remote machine when it is
                                ready to execute a command

    Returns:
        None
    """
    print("Restoring network conditions to normal on client!")
    # Get network interface names (excluding loopback)
    command = "sudo ifconfig -a | sed 's/[ ].*//;/^\(lo:\|\)$/d'"
    # Cannot use wait_until_cmd_done because we need to handle clase where ifconfig is not installed
    pexpect_process.sendline(command)

    ifconfig_output = wait_until_cmd_done(pexpect_process, pexpect_prompt, return_output=True)

    # Since we use ifconfig to apply network degradations, if ifconfig is not installed, we know
    # that no network degradations have been applied to the machine.
    error_msg = "sudo: ifconfig: command not found"

    if expression_in_pexpect_output(error_msg, ifconfig_output):
        print(
            "ifconfig is not installed on the client instance, so we don't need to restore normal network conditions."
        )
        return

    # Stop the process applying the artificial network conditions, in case it's still running
    command = "killall -9 -v apply_network_conditions.sh"
    pexpect_process.sendline(command)
    wait_until_cmd_done(pexpect_process, pexpect_prompt)

    # Get names of network devices
    blacklisted_expressions = [pexpect_prompt, "docker", "veth", "ifb", "ifconfig", "\\", "~", ";"]
    network_devices = [
        x.replace(":", "")
        for x in ifconfig_output
        if not any(
            blacklisted_expression in x for blacklisted_expression in blacklisted_expressions
        )
    ]

    commands = []
    for device in network_devices:
        print(f"Restoring normal network conditions on device {device}")
        # Inbound degradations
        commands.append(f"sudo tc qdisc del dev {device} handle ffff: ingress")
        # Outbound degradations
        commands.append(f"sudo tc qdisc del dev {device} root netem")

    commands.append("sudo modprobe -r ifb")

    # Execute all commands:
    for command in commands:
        pexpect_process.sendline(command)
        wait_until_cmd_done(pexpect_process, pexpect_prompt)
