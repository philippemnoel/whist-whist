#!/bin/bash

# Exit on subcommand errors
set -Eeuo pipefail

echo "Whist EC2 userdata started"

cd /home/ubuntu

sudo cat << EOF > /home/ubuntu/start-host-service.sh
#!/bin/bash

strace /home/ubuntu/host-service
EOF

# The Host Service gets built in the `whist-build-and-deploy.yml` workflow and
# uploaded from this Git repository to the AMI during Packer via ami_config.json
# Here, we write the systemd unit file for the Whist Host Service.
# Note that we do not restart the host service. This is because if the host
# service dies for some reason, it is not safe to restart it, since we cannot
# tell the status of the existing cloud storage directories. We just need to
# wait for them to unmount (that is an asynchronous process) and the webserver
# should declare the host dead and prune it.
sudo cat << EOF > /etc/systemd/system/host-service.service
[Unit]
Description=Host Service
Requires=docker.service
After=docker.service

[Service]
Restart=no
User=root
Type=exec
EnvironmentFile=/usr/share/whist/app_env.env
ExecStart=/home/ubuntu/start-host-service.sh

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
