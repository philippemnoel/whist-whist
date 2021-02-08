#!/bin/bash

# This script takes an EC2 instance already set up for running Fractal manually
# (for development) via setup_ubuntu20_host.sh and sets it up to run Fractal
# automatically (for production).

set -Eeuo pipefail

# Prevent user from running script as root, to guarantee that all steps are
# associated with the fractal user.
if [ "$EUID" -eq 0 ]; then
    echo "This script cannot be run as root!"
    exit
fi

# Create directories for ECS agent
sudo mkdir -p /var/log/ecs /var/lib/ecs/{data,gpu} /etc/ecs

# Install jq to build JSON
sudo apt-get install -y jq

# Create list of GPU devices for mounting to containers
DEVICES=""
for DEVICE_INDEX in {0..64}
do
  DEVICE_PATH="/dev/nvidia${DEVICE_INDEX}"
  if [ -e "$DEVICE_PATH" ]; then
    DEVICES="${DEVICES} --device ${DEVICE_PATH}:${DEVICE_PATH} "
  fi
done
DEVICE_MOUNTS=`printf "$DEVICES"`

# Set IP tables for routing networking from host to containers
sudo sh -c "echo 'net.ipv4.conf.all.route_localnet = 1' >> /etc/sysctl.conf"
sudo sysctl -p /etc/sysctl.conf
echo iptables-persistent iptables-persistent/autosave_v4 boolean true | sudo debconf-set-selections
echo iptables-persistent iptables-persistent/autosave_v6 boolean true | sudo debconf-set-selections
sudo apt-get install -y iptables-persistent
sudo iptables -t nat -A PREROUTING -p tcp -d 169.254.170.2 --dport 80 -j DNAT --to-destination 127.0.0.1:51679
sudo iptables -t nat -A OUTPUT -d 169.254.170.2 -p tcp -m tcp --dport 80 -j REDIRECT --to-ports 51679
sudo sh -c 'iptables-save > /etc/iptables/rules.v4'

# Create ECS agent config file
sudo mkdir -p /etc/ecs && sudo touch /etc/ecs/ecs.config
cat << EOF | sudo tee /etc/ecs/ecs.config
ECS_CLUSTER=default
ECS_DATADIR=/data
ECS_ENABLE_TASK_IAM_ROLE=true
ECS_ENABLE_TASK_IAM_ROLE_NETWORK_HOST=true
ECS_LOGFILE=/log/ecs-agent.log
ECS_AVAILABLE_LOGGING_DRIVERS=["syslog", "json-file", "journald", "awslogs"]
ECS_LOGLEVEL=info
ECS_ENABLE_GPU_SUPPORT=true
ECS_NVIDIA_RUNTIME=nvidia
EOF

# Remove extra unnecessary files
sudo rm -rf /var/lib/cloud/instances/
sudo rm -f /var/lib/ecs/data/*

# The ECS Host Service gets built in the `fractal-publish-ami.yml` workflow and 
# uploaded from this Git repository to the AMI during Packer via ami_config.json
# Here, we write systemd unit file for Fractal ECS Host Service
sudo cat << EOF > /etc/systemd/system/ecs-host-service.service
[Unit]
Description=ECS Host Service
Requires=docker.service
After=docker.service

[Service]
Restart=always
User=root
Type=exec
Environment="APP_ENV=PROD"
ExecStart=/home/ubuntu/ecs-host-service

[Install]
WantedBy=multi-user.target
EOF

# Reload daemon files
sudo /bin/systemctl daemon-reload

# Enable ECS Host Service
sudo systemctl enable --now ecs-host-service.service

echo
echo "Install complete. Make sure you do not reboot when creating the AMI (check NO REBOOT)"
echo
