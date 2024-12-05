#!/usr/bin/env python3

# For usage, run this script with `--help`.

import argparse
from collections import namedtuple
import os
import sys
import uuid

import docker
import psutil
import requests


DESCRIPTION = """
This script runs a Whist mandelbox by calling the `SpinUpMandelbox` endpoint
on the host service and emulating the Whist frontend  (but not the client protocol).
It is used by the scripts `run_local_mandelbox_image.sh` and
`run_remote_mandelbox_image.sh` and is usually not called directly.
"""


# Set current working directory to whist/mandelboxes
def reset_working_directory():
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    os.chdir("..")


reset_working_directory()

# Parse arguments
parser = argparse.ArgumentParser(description=DESCRIPTION)
parser.add_argument(
    "image",
    help="Whist mandelbox to run. Defaults to 'whisthq/base:current-build'.",
    default="whisthq/base:current-build",
)
parser.add_argument(
    "--host-address",
    help="IP address of the host service to send the request. Defaults to '127.0.0.1'.",
    default="127.0.0.1",
)
parser.add_argument(
    "--host-port",
    help="Port which the host service is listening to. Defaults to '4678'.",
    default="4678",
)
parser.add_argument(
    "--no-verify-tls",
    action="store_true",
    help=(
        "This flag skips TLS verification for requests to the host service. Not "
        "passing this flag means that this script will verify against the "
        "certificate '/whist/cert.pem'. By default, this flag is disabled."
    ),
)
parser.add_argument(
    "--no-kiosk",
    action="store_true",
    help="This flag launches browser apps in non-kiosk mode.",
)
parser.add_argument(
    "--no-extension",
    action="store_true",
    help="This flag launches browser apps without the extension.",
)
parser.add_argument(
    "--local-client",
    action="store_true",
    help="This flag indicates the frontend is being tested manually.",
)
args = parser.parse_args()


# Define some helper functions and variables
HOST_SERVICE_URL = f"https://{args.host_address}:{args.host_port}/"
HOST_SERVICE_CERT_PATH = "/whistprivate/cert.pem"
local_host_service = args.host_address == "127.0.0.1"
mandelbox_server_path = os.path.abspath("/usr/share/whist")

PortBindings = namedtuple(
    "PortBindings", ["host_port_32262tcp", "host_port_32263udp", "host_port_32273tcp"]
)


def ensure_root_privileges():
    if os.geteuid() != 0:
        sys.exit(
            (
                "You need to have root privileges to run this script.\nPlease try "
                "again, this time using 'sudo'. Exiting."
            )
        )


def get_public_ipv4_addr():
    if local_host_service:
        respobj = requests.get(url="https://checkip.amazonaws.com", timeout=10)
        respobj.raise_for_status()
        return respobj.text.strip()
    else:
        return args.host_address


def ensure_host_service_is_running():
    """
    Check if the host service is running by checking if there is a process
    listening on TCP port 4678.
    """
    running = any(
        c.status == "LISTEN" and c.laddr.port == 4678 for c in psutil.net_connections(kind="tcp")
    )
    if not running:
        sys.exit(
            (
                "Cannot start mandelbox because the host-service is not "
                "listening on port 4678. Is it running successfully?"
            )
        )


def kill_container(cont):
    """
    Takes in a Container object and kills it.
    """
    cont.kill()
    cont.remove()


def send_spin_up_mandelbox_request(mandelbox_id):
    """
    Sends the host service a SpinUpMandelbox request and returns a Container
    object corresponding to that mandelbox, along with its identifying host
    port (i.e. host port corresponding to tcp/32262 in the mandelbox) and
    aeskey.

    Args: mandelbox_id: the id of the mandelbox to create
    """
    print("Sending GetMandelbox request to host service!")
    url = HOST_SERVICE_URL + "mandelbox" + "/" + str(mandelbox_id)
    development_args = {
        "kiosk_mode": not args.no_kiosk,
        "load_extension": not args.no_extension,
        "local_client": args.local_client,
    }
    tls_verification = False if args.no_verify_tls else HOST_SERVICE_CERT_PATH

    try:
        respobj = requests.get(
            url=url, params=development_args, verify=tls_verification, timeout=10
        )
        respobj.raise_for_status()
    except requests.exceptions.RequestException as e:
        print(f"host service returned error: {e}")
        return

    response = respobj.json()
    print(f"Response from host service: {response}")
    respobj.raise_for_status()

    host_port_32262tcp = response["result"]["port_32262"]
    host_port_32263udp = response["result"]["port_32263"]
    host_port_32273tcp = response["result"]["port_32273"]
    key = response["result"]["aes_key"]

    return (
        PortBindings(host_port_32262tcp, host_port_32263udp, host_port_32273tcp),
        key,
    )


if __name__ == "__main__":
    # pylint: disable=line-too-long

    mandelboxid = uuid.uuid4()

    if local_host_service:
        # This is running locally on the same machine as the host service, so
        # we will need root privileges and can easily check whether the host
        # service is up.
        ensure_root_privileges()
        ensure_host_service_is_running()

        # This is running locally on the same machine as the host service, so
        # we can safely use the Docker client and copy files to the mandelbox.
        docker_client = docker.from_env()

    host_ports, aeskey = send_spin_up_mandelbox_request(mandelboxid)

    if local_host_service:
        # Find the Container object corresponding to the container that was just created
        matching_containers = docker_client.containers.list(
            filters={
                "publish": f"{host_ports.host_port_32262tcp}/tcp",
            }
        )
        assert len(matching_containers) == 1
        container = matching_containers[0]
    if "development/client" not in args.image:
        print(
            f"""Successfully started mandelbox with identifying hostPort {host_ports.host_port_32262tcp}.
    To connect to this mandelbox using the client protocol, run one of the following commands, depending on your platform:
        windows:        .\\wclient.bat {get_public_ipv4_addr()} -p32262:{host_ports.host_port_32262tcp}.32263:{host_ports.host_port_32263udp}.32273:{host_ports.host_port_32273tcp} -k {aeskey}
        linux/macos:    ./wclient {get_public_ipv4_addr()} -p32262:{host_ports.host_port_32262tcp}.32263:{host_ports.host_port_32263udp}.32273:{host_ports.host_port_32273tcp} -k {aeskey}
    """
        )

    if local_host_service:
        print(
            f"""Name of resulting mandelbox:
{container.name}
Docker ID of resulting mandelbox:"""
        )
        # Note that ./run_mandelbox_image.sh relies on the dockerID being printed
        # alone on the last line of the output of this script!!
        print(f"{container.id}")
