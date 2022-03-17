#!/bin/bash

# Exit on subcommand errors
set -Eeuo pipefail

echo "Whist EC2 userdata started"

cd /home/ubuntu

# The first thing we want to do is to set up the ephemeral storage available on
# our instance, if there is some.
EPHEMERAL_DEVICE_PATH=$(sudo nvme list -o json | jq -r '.Devices | map(select(.ModelNumber == "Amazon EC2 NVMe Instance Storage")) | max_by(.PhysicalSize) | .DevicePath')
EPHEMERAL_FS_PATH=/ephemeral

if [ $EPHEMERAL_DEVICE_PATH != "null" ]
then
    echo "Ephemeral device path found: $EPHEMERAL_DEVICE_PATH"

    sudo mkfs -t ext4 "$EPHEMERAL_DEVICE_PATH"
    sudo mkdir -p "$EPHEMERAL_FS_PATH"
    sudo mount "$EPHEMERAL_DEVICE_PATH" "$EPHEMERAL_FS_PATH"
    echo "Mounted ephemeral storage at $EPHEMERAL_FS_PATH"

    # Stop docker and copy the data directory that contains mandelbox images to the ephemeral storage
    sudo systemctl stop docker
    sudo mv /var/lib/docker "$EPHEMERAL_FS_PATH"

    # Modify configuration to use the new data directory and persist in daemon config file
    # and start docker again
    jq '. + {"data-root": "'"$EPHEMERAL_FS_PATH/docker"'"}' /etc/docker/daemon.json > tmp.json && sudo mv tmp.json /etc/docker/daemon.json
    sudo systemctl start docker

    # Write ephemeral path and device to a file so host service can read them
    # TODO: write to file so host service can read them

else
    echo "No ephemeral device path found. Warming up EBS volume with fio."
    # Warm Up EBS Volume
    # For more information, see: https://github.com/whisthq/whist/pull/5333
    sudo fio --filename=/dev/nvme0n1 --rw=read --bs=128k --iodepth=32 --ioengine=libaio --direct=1 --name=volume-initialize

fi




# The Host Service gets built in the `whist-build-and-deploy.yml` workflow and
# uploaded from this Git repository to the AMI during Packer via ami_config.json
# Here, we write the systemd unit file for the Whist Host Service.
# Note that we do not restart the host service. This is because if the host
# service dies for some reason, it is not safe to restart it, since we cannot
# tell the status of the existing cloud storage directories. We just need to
# wait for them to unmount (that is an asynchronous process) and the webserver
# should declare the host dead and prune it.
cat << EOF | sudo tee /etc/systemd/system/host-service.service
[Unit]
Description=Host Service
Requires=docker.service
After=docker.service

[Service]
Restart=no
User=root
Type=exec
EnvironmentFile=/usr/share/whist/app_env.env
ExecStart=/home/ubuntu/host-service

[Install]
WantedBy=multi-user.target
EOF

echo "Created systemd service for host-service"

# Reload daemon files
sudo /bin/systemctl daemon-reload

# Enable Host Service
sudo systemctl enable --now host-service.service

echo "Enabled host-service"

echo "Whist EC2 userdata finished"
