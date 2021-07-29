#!/bin/bash

# This script takes a blank EC2 instance and sets it up for running Fractal manually (for development), by
# configuring Docker and NVIDIA to run Fractal's GPU-enabled mandelboxes. This script is all you need if you
# are setting up an EC2 instance for development, and is part 1/2, with setup_ubuntu20_host_ami.sh, if you
# are creating a base AMI to run Fractal in production.

set -Eeuo pipefail

# Retrieve source directory of this script
# https://stackoverflow.com/questions/59895/how-to-get-the-source-directory-of-a-bash-script-from-within-the-script-itself
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd "$DIR"

# If we run this script as root, then the "ubuntu"/default user will not be
# added to the "docker" group, only root will.
if [ "$EUID" -eq 0 ]; then
    echo "This script cannot be run as root!"
    exit
fi

# Set dkpg frontend as non-interactive to avoid irrelevant warnings
echo 'debconf debconf/frontend select Noninteractive' | sudo debconf-set-selections
sudo apt-get install -y -q

echo "================================================"
echo "Replacing potentially outdated Docker runtime..."
echo "================================================"

# Attempt to remove potentially outdated Docker runtime
# Allow failure with ||:, in case they're not installed yet
sudo apt-get remove -y docker docker-engine docker.io containerd runc ||:
sudo apt-get clean -y
sudo apt-get upgrade -y
sudo apt-get update -y

# Install latest Docker runtime and dependencies
sudo apt-get install -y apt-transport-https ca-certificates curl wget gnupg-agent software-properties-common
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add -
sudo apt-key fingerprint 0EBFCD88
sudo add-apt-repository \
    "deb [arch=amd64] https://download.docker.com/linux/ubuntu \
   $(lsb_release -cs) \
    stable"
sudo apt-get update -y
sudo apt-get install -y docker-ce docker-ce-cli containerd.io

# Attempt to Add Docker group, but allow failure with "||:" in case it already exists
sudo groupadd docker ||:
sudo gpasswd -a "$USER" docker

echo "================================================"
echo "Installing AWS CLI..."
echo "================================================"

# We don't need to configure the AWS CLI (only install it) because this script runs
# on an AWS EC2 instance, which have awscli automatically configured
sudo apt install -y awscli

echo "================================================"
echo "Installing NVIDIA drivers..."
echo "================================================"

# Stop any running nvidia-persistenced service
sudo systemctl stop nvidia-persistenced.service ||:

# Install Linux headers
sudo apt-get install -y gcc make "linux-headers-$(uname -r)"

# Blacklist some Linux kernel modules that would block NVIDIA drivers
cat << EOF | sudo tee --append /etc/modprobe.d/blacklist.conf
blacklist vga16fb
blacklist nouveau
blacklist rivafb
blacklist nvidiafb
blacklist rivatv
EOF
sudo sed -i 's/GRUB_CMDLINE_LINUX=""/# GRUB_CMDLINE_LINUX=""/g' /etc/default/grub
cat << EOF | sudo tee --append /etc/default/grub
GRUB_CMDLINE_LINUX="rdblacklist=nouveau"
EOF

# Install NVIDIA GRID (virtualized GPU) drivers
./get-nvidia-driver-installer.sh
sudo chmod +x nvidia-driver-installer.run
sudo ./nvidia-driver-installer.run --silent
sudo rm nvidia-driver-installer.run

echo "================================================"
echo "Installing nvidia-docker..."
echo "Note that (as of 10/5/20) the URLs may still say Ubuntu 18.04. This is because"
echo "NVIDIA has redirected the corresponding Ubuntu 20.04 URLs to the 18.04 versions."
echo "================================================"

# Source nvidia-docker apt package
distribution=$(. /etc/os-release;echo $ID$VERSION_ID)
curl -s -L https://nvidia.github.io/nvidia-docker/gpgkey | sudo apt-key add -
curl -s -L https://nvidia.github.io/nvidia-docker/$distribution/nvidia-docker.list | sudo tee /etc/apt/sources.list.d/nvidia-docker.list

# Install nvidia-docker via apt
sudo apt-get update -y
sudo apt-get install -y nvidia-docker2

echo "================================================"
echo "Installing General Utilities..."
echo "================================================"

sudo apt-get install -y lsof jq tar lz4

echo "================================================"
echo "Configuring Docker daemon..."
echo "================================================"

# Set custom seccomp filter
sudo systemctl start docker
./docker-daemon-config/generate-seccomp.sh

sudo cp docker-daemon-config/daemon.json /etc/docker/daemon.json
sudo cp docker-daemon-config/seccomp.json /etc/docker/seccomp.json

sudo systemctl restart docker

echo "================================================"
echo "Installing Other Host Service Dependencies..."
echo "================================================"

sudo apt-get install -y openssl

echo "================================================"
echo "Installing Uinput Config Files..."
echo "================================================"

sudo cp fractal-input.rules /etc/udev/rules.d/90-fractal-input.rules

echo "================================================"
echo "Installing monitoring services.."
echo "================================================"
wget -qO - https://artifacts.elastic.co/GPG-KEY-elasticsearch | sudo apt-key add -
echo "deb https://artifacts.elastic.co/packages/7.x/apt stable main" | sudo tee -a /etc/apt/sources.list.d/elastic-7.x.list

# Install filebeat via apt
sudo apt-get update -y
sudo apt-get install -y filebeat

# For HTTPS shipping, download the Logz.io public certificate to your certificate authority folder. 
# https://app.logz.io/#/dashboard/send-your-data/log-sources/filebeat
sudo curl https://raw.githubusercontent.com/logzio/public-certificates/master/TrustExternalCARoot_and_USERTrustRSAAAACA.crt --create-dirs -o /etc/pki/tls/certs/COMODORSADomainValidationSecureServerCA.crt

# filebeat by default doesn't startup on system boot/install by default.
# So, filebeat will be currently disabled. This being done intentionally to not ship logs from local-dev.
# If you want to enable it, look at `setup_ubuntu20_ami_host.sh` for further instructions.

echo "================================================"
echo "Updating resource limits..."
echo "================================================"
# We use inotify to communicate between mandelboxes and the host service. We
# use at least one per mandelbox, and Docker itself seems to use quite a few,
# so we don't want this resource to be a limiting factor.
sudo sh -c "echo 'fs.inotify.max_user_instances=2048' >> /etc/sysctl.conf" # default=128
sudo sysctl -p

echo "================================================"
echo "Installing NVIDIA Persistence Daemon Unit..."
echo "================================================"

# Some research online indicates that this might be beneficial towards ensuring that our mandelboxes
# start up more quickly. This is likely not a complete solution, but it might provide some nice benefits.
# For more information, see the following link:
# https://download.nvidia.com/XFree86/Linux-x86_64/396.51/README/nvidia-persistenced.html

cat << EOF | sudo tee /etc/systemd/system/nvidia-persistenced.service > /dev/null
[Unit]
Description=NVIDIA Persistence Daemon
Wants=syslog.target

[Service]
Restart=yes
User=root
Type=forking
ExecStart=/usr/bin/nvidia-persistenced -V
ExecStopPost=/bin/rm -rf /var/run/nvidia-persistenced

[Install]
WantedBy=multi-user.target
EOF

sudo /bin/systemctl daemon-reload
sudo systemctl enable --now nvidia-persistenced.service
echo "Enabled NVIDIA Persistence Daemon"

echo "================================================"
echo "Cleaning up the image a bit..."
echo "================================================"

sudo apt-get autoremove -y

echo
echo "Install complete. Please 'sudo reboot' before continuing."
echo
