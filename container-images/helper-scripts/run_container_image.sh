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

# Set working directory to fractal/container-images
cd "$DIR/.."

# Fractal container image to run
image=${1:-fractal/base:current-build}
app_name=$(echo "$image" | sed 's/.*fractal\/\(.*\):.*/\1/')

# Define the folder to mount the Fractal protocol server into the container
if [[ "$2" == mount ]]; then
    mount_protocol="--mount type=bind,source=$(cd ../protocol/build-docker/server/build64; pwd),destination=/usr/share/fractal/bin"
else
    mount_protocol=""
fi

# DPI to set the X Server to inside the container, defaults to 96 (High Definition) if not set
dpi=${FRACTAL_DPI:-96}

# User ID to use for user config retrieval
# TODO: This flow will have to change when we actually encrypt the configs.
# (https://github.com/fractal/fractal/issues/1136)
user_id=${FRACTAL_USER_ID:-''}

# timeout to set the protocol server to inside the container, defaults to 60 seconds if not set
protocol_timeout=${FRACTAL_TIMEOUT:-60}

# User config encryption token. This would normally be passed in by the client app,
# but we'll use a fake key here.
config_encryption_token="RaR9Olgvqj+/AtNUHAPXjRZ26FkrFIVd"

# User access token. This would normally be passed in by the webserver and client app,
# but we'll use a fake token here.
user_access_token="xTt/D6bqW7wfRTCcA6dVKqROl0iadS5k"

# Helper function to kill a locally running Docker container
# Args: container_id
kill_container() {
    docker kill "$1" > /dev/null || true
    docker rm "$1" > /dev/null || true
}

# Helper function to print error message and then kill a locally running Docker container
# Args: container_id, error_message
print_error_and_kill_container() {
    echo "$2" && kill_container "$1" && exit 1
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
# Args: container_id, DPI, user_id, access_token
send_start_values_request() {
    echo "Sending container start values request to container $1!"
    # Send the DPI/container-ready request
    response=$(curl --insecure --silent --location --request PUT 'https://localhost:4678/set_container_start_values' \
            --header 'Content-Type: application/json' \
            --data-raw '{
      "auth_secret": "testwebserverauthsecretdev",
      "host_port": 32262,
      "dpi": '"$2"',
      "user_id": "'"$3"'",
      "client_app_auth_secret": "'"$4"'"
    }') \
        || (print_error_and_kill_container $1 "container start values request to the host service failed!")

    echo "Sent container start values request to container $1!"
    echo "Response to container start values request from host service: $response"
}

# Send a set config encryption token request to the Fractal ECS host service HTTP server running on localhost
# This is also necessary for the Fractal server protocol to think that it is ready to start. In production,
# the client app would send this request to the Fractal host service, but for local development we need
# to send it manually until our development pipeline is fully built
# Args: container_id, user_id, config_encryption_token, access_token
send_set_config_encryption_token_request() {
    # Send the config encryption token
    response=$(curl --insecure --silent --location --request PUT 'https://localhost:4678/set_config_encryption_token' \
            --header 'Content-Type: application/json' \
            --data-raw '{
      "auth_secret": "testwebserverauthsecretdev",
      "host_port": 32262,
      "user_id": "'"$2"'",
      "config_encryption_token": "'"$3"'",
      "client_app_auth_secret": "'"$4"'"
    }') \
        || (print_error_and_kill_container $1 "set config encryption token request to the host service failed!")

    echo "Sent set config encryption token request to container $1!"
    echo "Response to set config encryption token request from host service: $response"
}

# Send a spin_up_container request to the host service (which must be running with `make run`).
# Args: app_name, app_image, mount_command, timeout. Modifies container_id
send_spin_up_container_request() {
    # Send the request
    echo "Sending spin up request to host service!"
    response=$(curl --insecure --silent --location --request PUT 'https://localhost:4678/spin_up_container' \
            --header 'Content-Type: application/json' \
            --data-raw '{
      "auth_secret": "testwebserverauthsecretdev",
      "app_name": "'"$1"'",
      "app_image": "'"$2"'",
      "mount_command": "'"$3"'",
      "protocol_timeout": '"$4"'
    }') \
        || (echo "spin up request to the host service failed!" && exit 1)
    echo "Response to spin up request from host service: $response"
    container_id=$(jq -r '.result' <<< "$response")
    echo "Container ID: $container_id"
}


# Main executing thread
container_id=""
check_if_host_service_running
send_spin_up_container_request "$app_name" "$image" "$mount_protocol" "$protocol_timeout"
send_start_values_request "$container_id" "$dpi" "$user_id" "$user_access_token"
send_set_config_encryption_token_request "$container_id" "$user_id" "$config_encryption_token" "$user_access_token"

# Run bash inside the Docker container
docker exec -it "$container_id" /bin/bash || true

# Kill the container once we're done
kill_container "$container_id"
