#!/bin/bash

# This script runs a Fractal container by executing the `docker run` command with the right
# arguments, and also containers helper functions for manually killing a container and for
# manually sending a DPI request to a locally-run container (two tasks handled by the Fractal
# webserver, in production), which facilitates local development.

set -Eeuo pipefail

# Retrieve relative subfolder path
# https://stackoverflow.com/questions/59895/how-to-get-the-source-directory-of-a-bash-script-from-within-the-script-itself
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
# Working directory is fractal/container-images/helper-scripts/
cd "$DIR/.."

# Fractal container image to run
image=${1:-fractal/base:current-build}

# Define the folder to mount the Fractal protocol server into the container
if [[ ${2:-''} == mount ]]; then
    mount_protocol="--mount type=bind,source=$(cd base/protocol/server/build64;pwd),destination=/usr/share/fractal/bin"
else
    mount_protocol=""
fi

# DPI to set the X Server to inside the container, defaults to 96 (High Definition) if not set
dpi=${FRACTAL_DPI:-96}

# User ID to use for user config retrieval
# TODO: This flow will have to change when we actually encrypt the configs.
user_id=${FRACTAL_USER_ID}


# FractalID of the container. This would normally be randomly generated by the
# ecs-agent, but we need to simulate it here.
fractal_id="abcdefabcdefabcdefabcdefabcdef"

# Devices to pas into the container
devices_arg=""

# Create the specified Docker container image with as-restrictive-as-possible system
# capabilities, for security purposes
create_container() {
    docker create -it \
        -v "/sys/fs/cgroup:/sys/fs/cgroup:ro" \
        -v "/fractal/$fractal_id/containerResourceMappings:/fractal/resourceMappings:ro" \
        -v "/fractalCloudStorage/$fractal_id:/fractal/cloudStorage:rshared" \
        -v "/fractal/temp/$fractal_id/sockets:/tmp/sockets" \
        -v "/run/udev/data:/run/udev/data:ro" \
        -v "/fractal/userConfigs:/fractal/userConfigs:rshared" \
        $devices_arg \
        $mount_protocol \
        --tmpfs /run \
        --tmpfs /run/lock \
        --gpus all \
        -e NVIDIA_CONTAINER_CAPABILITIES=all \
        -e NVIDIA_VISIBLE_DEVICES=all \
        --shm-size=8g \
        --cap-drop ALL \
        --cap-add CAP_SETPCAP \
        --cap-add CAP_MKNOD \
        --cap-add CAP_AUDIT_WRITE \
        --cap-add CAP_CHOWN \
        --cap-add CAP_NET_RAW \
        --cap-add CAP_DAC_OVERRIDE \
        --cap-add CAP_FOWNER \
        --cap-add CAP_FSETID \
        --cap-add CAP_KILL \
        --cap-add CAP_SETGID \
        --cap-add CAP_SETUID \
        --cap-add CAP_NET_BIND_SERVICE \
        --cap-add CAP_SYS_CHROOT \
        --cap-add CAP_SETFCAP \
        --cap-add SYS_NICE \
        -p 32262:32262 \
        -p 32263:32263/udp \
        -p 32273:32273 \
        $image
    # capabilities not enabled by default: CAP_NICE
}

# Helper function to kill a locally running Docker container
# Args: container_id
kill_container() {
  docker kill $1 > /dev/null || true
  docker rm $1 > /dev/null || true
}

# Helper function to print error message and then kill a locally running Docker container
# Args: container_id, error_message
print_error_and_kill_container() {
  echo "$2" && kill_container $1 && exit 1
}

# Check if host service is even running
# Args: none
check_if_host_service_running() {
  sudo lsof -i :4678 | grep ecs-host > /dev/null \
  || (echo "Cannot start container because the ecs-host-service is not listening on port 4678. Is it running successfully?" && exit 1)
}

# Send a start values request to the Fractal ECS host service HTTP server running on localhost
# This is necessary for the Fractal server protocol to think that it is ready to start. In production,
# the webserver would send this request to the Fractal host service, but for local development we need
# to send it manually until our development pipeline is fully built
# Args: container_id, DPI, user_id
send_start_values_request() {
  # Send the DPI/container-ready request
  response=$(curl --insecure --silent --location --request PUT 'https://localhost:4678/set_container_start_values' \
    --header 'Content-Type: application/json' \
    --data-raw '{
      "auth_secret": "testwebserverauthsecretdev",
      "host_port": 32262,
      "dpi": '"$2"',
      "user_id": '"$3"'
    }') \
  || (print_error_and_kill_container $1 "DPI/container-ready request to the host service failed!")
  echo "Sent DPI/container-ready request to container $1!"
  echo "Response to DPI/container-ready request from host service: $response"
}

# Send a request to the host service (pretending to be the ecs-agent) setting
# up the mapping between FractalID and DockerID.
# Args: container_id, fractal_id
send_register_docker_container_id_request() {
  # Send the request
  response=$(curl --insecure --silent --location --request POST 'https://localhost:4678/register_docker_container_id' \
    --header 'Content-Type: application/json' \
    --data-raw '{
      "auth_secret": "testwebserverauthsecretdev",
      "docker_id": "'"$1"'",
      "fractal_id": "'"$2"'"
    }') \
  || (print_error_and_kill_container $1 "register_docker_container_id request to the host service failed!")
  echo "Sent register_docker_container_id request to container $1!"
  echo "Response to register_docker_container_id request from host service: $response"
}

# Send a request to the host service (pretending to be the ecs-agent) asking
# for the creation of uinput devices for the container. For now, we just assume
# that the returned devices will be /dev/input/event{3,4,5}
# Args: fractal_id
send_uinput_device_request() {
  # Send the request
  response=$(curl --insecure --silent --location --request POST 'https://localhost:4678/create_uinput_devices' \
    --header 'Content-Type: application/json' \
    --data-raw '{
      "auth_secret": "testwebserverauthsecretdev",
      "fractal_id": "'"$1"'"
    }') \
  || (echo "create_uinput_devices request to the host service failed!" && exit 1)
  echo "Sent create_uinput_devices request for container with FractalID $1!"
  echo "Response to create_uinput_devices request from host service: $response"
  host_paths=($((jq -r '.result | fromjson | .[].path_on_host | @sh' <<< "$response") | tr -d \'))
  container_paths=($((jq -r '.result | fromjson | .[].path_in_container | @sh' <<< "$response") | tr -d \'))
  device_perms=($((jq -r '.result | fromjson | .[].cgroup_permissions | @sh' <<< "$response") | tr -d \'))
  devices_arg=$(printf -- "--device=%s:%s:%s\n--device=%s:%s:%s\n--device=%s:%s:%s\n" \
    "${host_paths[0]}" "${container_paths[0]}" "${device_perms[0]}" \
    "${host_paths[1]}" "${container_paths[1]}" "${device_perms[1]}" \
    "${host_paths[2]}" "${container_paths[2]}" "${device_perms[2]}" \
  )
}


# Main executing thread
check_if_host_service_running
send_uinput_device_request $fractal_id
container_id=$(create_container $image)
echo "Created container with ID: $container_id"
send_register_docker_container_id_request $container_id $fractal_id
docker start $container_id
send_start_values_request $container_id $dpi $user_id

# Run the Docker container
docker exec -it $container_id /bin/bash || true

# Kill the Docker container once we are done
kill_container $container_id
