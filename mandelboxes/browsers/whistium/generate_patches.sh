#!/bin/bash

set -Eeuo pipefail

WHISTIUM_INSTANCE_IP=$1
WHISTIUM_INSTANCE_SSH_KEY=$2
WHISTIUM_INSTANCE_CHROMIUM_PATH=$3
CHROMIUM_BASE_VERSION=$4
MONOREPO_PATH=$(git rev-parse --show-toplevel)

# Check for confirmation
read -r -e -p "This script will delete all patches and re-generate them. Interrupting this script may yield undesired results. Continue? [y/N] " choice
[[ "$choice" == [Yy]* ]] && echo "Beginning patch generation process!" || exit

# Delete and recreate patch folder
PATCH_FOLDER="$MONOREPO_PATH"/mandelboxes/browsers/whistium/patches
echo "Deleting and recreating local patch directory..."
rm -rf "$PATCH_FOLDER"
mkdir -p "$PATCH_FOLDER"

# Create patches
echo "Creating patch files..."
# This command does the following:
# - Remove and recreate temporary patches directory
# - Declare "intent to add" untracked files to make them tracked
# - Generate diffs for all modified and added files into the temporary patches directory
ssh -i "$WHISTIUM_INSTANCE_SSH_KEY" ubuntu@"$WHISTIUM_INSTANCE_IP" "cd $WHISTIUM_INSTANCE_CHROMIUM_PATH && rm -rf /tmp/patches && mkdir /tmp/patches && git add -N . && for filepath in \$(git diff $CHROMIUM_BASE_VERSION --name-only) ; do git diff -p $CHROMIUM_BASE_VERSION \$filepath > /tmp/patches/\$(echo \$filepath | tr \"/\" \"-\").patch ; done"
scp -i "$WHISTIUM_INSTANCE_SSH_KEY" ubuntu@"$WHISTIUM_INSTANCE_IP":/tmp/patches/* "$PATCH_FOLDER"
