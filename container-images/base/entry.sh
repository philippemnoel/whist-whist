#!/bin/bash
# Amazon linux doesn't have this
# echo "Setting up Fractal Firewall"
# source /setup-scripts/linux/utils.sh && Enable-FractalFirewallRule
# yes | ufw allow 5900;

# Allow for ssh login
rm /var/run/nologin
# echo $SSH_PUBLIC_KEY_AWS > ~/.ssh/authorized_keys

rm /etc/udev/rules.d/90-fractal-input.rules
ln -sf /home/fractal/fractal-input.rules /etc/udev/rules.d/90-fractal-input.rules
# echo "Entry.sh handing off to bootstrap.sh" 

# Create a tty within the container so we don't have to hook it up to one of the host's
# Note that this CANNOT be done in the Dockerfile because it affects /dev/ so we have
# to do it here.
sudo mknod -m 620 /dev/tty10 c 4 10

# This from setup-scripts install fractal service
echo "Start Pam Systemd Process for User fractal"
export FRACTAL_UID=`id -u fractal`
install -d -o fractal /run/user/$FRACTAL_UID
systemctl start user@$FRACTAL_UID
