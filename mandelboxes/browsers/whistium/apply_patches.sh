#!/bin/bash

set -Eeuo pipefail

WHISTIUM_INSTANCE_IP=$1
WHISTIUM_INSTANCE_SSH_KEY=$2
WHISTIUM_INSTANCE_CHROMIUM_PATH=$3
CHROMIUM_BASE_VERSION=$4
MONOREPO_PATH=$(git rev-parse --show-toplevel)

# Check for confirmation
read -r -e -p "If you have any local changes you want to save before applying patches, remember to git stash in your Chromium repository first because they will be lost! Continue? [y/N] " choice
[[ "$choice" == [Yy]* ]] && echo "Beginning patch application process!" || exit

# Reset Chromium repo to base version
echo "Resetting Chromium to version $CHROMIUM_BASE_VERSION..."
ssh -i "$WHISTIUM_INSTANCE_SSH_KEY" ubuntu@"$WHISTIUM_INSTANCE_IP" "cd $WHISTIUM_INSTANCE_CHROMIUM_PATH && git checkout -f $CHROMIUM_BASE_VERSION && git clean -df"

# Apply patches
echo "Applying patches..."
# The following two commands remove and create the temporary patches directory and store the patches in that directory
ssh -i "$WHISTIUM_INSTANCE_SSH_KEY" ubuntu@"$WHISTIUM_INSTANCE_IP" "rm -rf /tmp/patches && mkdir /tmp/patches"
scp -i "$WHISTIUM_INSTANCE_SSH_KEY" "$MONOREPO_PATH"/mandelboxes/browsers/whistium/patches/* ubuntu@"$WHISTIUM_INSTANCE_IP":/tmp/patches
ssh -i "$WHISTIUM_INSTANCE_SSH_KEY" ubuntu@"$WHISTIUM_INSTANCE_IP" "cd $WHISTIUM_INSTANCE_CHROMIUM_PATH && git apply /tmp/patches/*"

# Commit changes locally to allow easy diff-checking
echo "Committing applied changes..."
ssh -i "$WHISTIUM_INSTANCE_SSH_KEY" ubuntu@"$WHISTIUM_INSTANCE_IP" "cd $WHISTIUM_INSTANCE_CHROMIUM_PATH && git add . && git commit -m 'applied monorepo patches'"
