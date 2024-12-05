#!/bin/bash

# Exit on subcommand errors
set -Eeuo pipefail

sudo apt-get update

# Install ffmpeg dependencies
sudo apt-get install --no-install-recommends -y \
  libmfx1 \
  libvo-amrwbenc0 \
  libopencore-amrnb0 \
  libopencore-amrwb0 \
  libx264-155 \
  libx265-179 \
  libopus-dev \
  libfdk-aac1 \
  libva-drm2 \
  libvdpau1

# Install Whist dependencies
sudo apt-get install --no-install-recommends -y \
  libc++1 \
  dbus-x11 \
  libevent-dev \
  libevent-pthreads-2.1-7 \
  libdbus-1-dev \
  libssl-dev \
  libgl1-mesa-dev \
  libavcodec-dev \
  libavdevice-dev \
  libx11-dev \
  libxtst-dev \
  xclip \
  x11-xserver-utils \
  libasound2-dev \
  libxdamage-dev \
  libcurl4-openssl-dev \
  nvidia-cuda-toolkit \
  zlib1g-dev \
  awesome

