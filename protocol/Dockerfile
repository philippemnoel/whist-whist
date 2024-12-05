FROM ubuntu:20.04
ARG uid

# This Dockerfile is used to build the Whist protocol server for Linux Ubuntu 20.04.

# Force failure on non-zero exit code for commands with a pipe in them
SHELL ["/bin/bash", "-Eeuo", "pipefail", "-c"]

# Set protocol builder environment variables
ENV container docker
ENV LC_ALL C
ENV DEBIAN_FRONTEND noninteractive
RUN sed -i 's/# deb/deb/g' /etc/apt/sources.list

# Turn off interactive mode
RUN echo 'debconf debconf/frontend select Noninteractive' | debconf-set-selections

# Install and configure Ubuntu utils
RUN apt-get update && apt-get install --allow-downgrades --no-install-recommends -y \
    apt-utils \
    wget \
    curl \
    git \
    xz-utils \
    apt-transport-https \
    ca-certificates \
    gnupg \
    software-properties-common \
    && apt-get autoremove -y \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

RUN wget -qO - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | APT_KEY_DONT_WARN_ON_DANGEROUS_USAGE=1 apt-key add -

RUN apt-add-repository 'deb https://apt.kitware.com/ubuntu/ focal main'

RUN apt-get update \
    && apt-get autoremove -y \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

RUN dpkg --configure -a && apt-get install --allow-downgrades --no-install-recommends -y -f

# Install build tools & `docs` target dependencies
RUN apt-get update && apt-get install --allow-downgrades --no-install-recommends -y \
    # build tools
    make \
    gcc \
    g++ \
    cppcheck \
    clang-tidy \
    cmake \
    cmake-data \
    sudo \
    clang-format \
    awscli \
    lcov \
    jq \
    build-essential \
    # `docs` target dependencies
    doxygen \
    coreutils \
    xvfb \
    awesome \
    perl \
    && apt-get autoremove -y \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

RUN echo "Set disable_coredump false" >> /etc/sudo.conf

# Install Whist protocol Linux dependencies
COPY setup-linux-build-environment.sh /
RUN apt-get update \
    && ./setup-linux-build-environment.sh \
    && apt-get autoremove -y \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# create /tmp/.X11-unix so we're able to run xvfb
RUN mkdir -p /tmp/.X11-unix \
	&& chmod 0777 /tmp/.X11-unix \
	&& chmod +t /tmp/.X11-unix

# Install notification utilities: dbus and libevent
RUN apt-get update && apt-get install --allow-downgrades --no-install-recommends -y \
    libdbus-1-dev \
    libevent-dev \
    libevent-pthreads-2.1-7 \
    && apt-get autoremove -y \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Create whist Linux user for building the protocol
RUN groupadd whist \
    && useradd -lou ${uid} -g whist whist-builder \
    && usermod -aG sudo whist-builder \
    && echo "whist-builder:password" \
    | chpasswd

# Specify workdir for building the protocol
WORKDIR /workdir
