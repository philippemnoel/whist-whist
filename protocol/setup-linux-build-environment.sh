#!/bin/bash

# Exit on subcommand errors
set -Eeuo pipefail

# Install ffmpeg dependencies
sudo apt-get install -y \
    libmfx1 libvo-amrwbenc0 libopencore-amrnb0 libopencore-amrwb0 libx264-155 libx265-179 libfdk-aac1 libva-drm2 libvdpau1

# Install Fractal dependencies

sudo apt-get install -y \
    libssl-dev \
    libx11-dev \
    libxtst-dev \
    xclip \
    x11-xserver-utils \
    libasound2-dev \
    libxdamage-dev \
    libcurl4-openssl-dev \
    zlib1g-dev
