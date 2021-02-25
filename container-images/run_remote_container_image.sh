#!/bin/bash

# This script runs a container image stored in GitHub Container Registry (GHCR). For  it to
# work with the Fractal containers, this script needs to be run directly on a Fractal-enabled
# (see /ecs-host-setup) AWS EC2 instance, via SSH, without needing to build a container image beforehand.

# Exit on errors and missing environment variables
set -Eeuo pipefail

# Retrieve relative subfolder path
# https://stackoverflow.com/questions/59895/how-to-get-the-source-directory-of-a-bash-script-from-within-the-script-itself
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
# Working directory is fractal/container-images/
cd "$DIR"

# Parameters of the GHCR-stored container image to run
app_path=${1%/}
repo_name=fractal/$app_path
remote_tag=$2
mount=${3:-}
ghcr_uri=ghcr.io
image=$ghcr_uri/$repo_name:$remote_tag

# Authenticate to GHCR
echo "$GH_PAT" | docker login --username "$GH_USERNAME" --password-stdin $ghcr_uri

# Download the container image from GHCR
docker pull "$image"

# Run the container image retrieved from GHCR
./helper-scripts/run_container_image.sh "$image" "$mount"
