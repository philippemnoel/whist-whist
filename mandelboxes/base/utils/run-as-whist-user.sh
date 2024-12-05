#!/bin/bash

WHIST_PRIVATE_DIR=/usr/share/whist/private
# Enable Sentry bash error handler, this will catch errors if `set -e` is set in a Bash script
SENTRY_ENV_FILENAME=$WHIST_PRIVATE_DIR/sentry_env
case $(cat $SENTRY_ENV_FILENAME) in
  dev|staging|prod)
    export SENTRY_ENVIRONMENT=${SENTRY_ENV}
    eval "$(sentry-cli bash-hook)"
    ;;
  *)
    echo "Sentry environment not set, skipping Sentry error handler"
    ;;
esac

# Exit on subcommand errors
set -Eeuo pipefail

# Things running outside run_as_whist are run as root.
# Running with login means that we lose all environment variables, so we need to pass them in manually.

# Default to mac for now
CLIENT_OS="darwin"
# Set system language to "en_US" until we can do a restart-less modification of system language.
SYSTEM_LANGUAGES="en_US"

# TODO: set the following to development flags
LOAD_EXTENSION_FILENAME=$WHIST_PRIVATE_DIR/load_extension
KIOSK_MODE_FILENAME=$WHIST_PRIVATE_DIR/kiosk_mode

LOAD_EXTENSION=true
KIOSK_MODE=true
# Send in LOAD_EXTENSION and KIOSK_MODE, if set
if [ -f "$LOAD_EXTENSION_FILENAME" ]; then
  LOAD_EXTENSION=$(cat $LOAD_EXTENSION_FILENAME)
fi
if [ -f "$KIOSK_MODE_FILENAME" ]; then
  KIOSK_MODE=$(cat $KIOSK_MODE_FILENAME)
fi

# Set keyboard repeat to "on"
xset r on
if [[ "$CLIENT_OS" == "darwin" ]]; then
  # Most keys on macOS do not repeat, but all keys repeat on Linux. We turn off key repeat on certain Linux keys
  # to match the macOS behavior. This needs to be done *after* setting the key repeat rate above.
  # We have decided to allow numbers to repeat because, even though Macs don't repeat numbers,
  # they do repeat things like exclamation marks.
  # Keycodes obtianed from: https://gist.github.com/rickyzhang82/8581a762c9f9fc6ddb8390872552c250#file-keycode-linux-L91
  keys_to_turn_off_repeat=(
    9   # ESC
    67  # F1
    68  # F2
    69  # F3
    70  # F4
    71  # F5
    72  # F6
    73  # F7
    74  # F8
    75  # F9
    76  # F10
    95  # F11
    96  # F12
    78  # Scroll Lock
    110 # Pause
    106 # Insert
    77  # Num Lock
    24  # Q
    25  # W
    26  # E
    27  # R
    28  # T
    29  # Y
    30  # U
    31  # I
    32  # O
    33  # P
    66  # Caps Lock
    38  # A
    39  # S
    40  # D
    41  # F
    42  # G
    43  # H
    44  # J
    45  # K
    46  # L
    52  # Z
    53  # X
    54  # C
    55  # V
    56  # B
    57  # N
    58  # M
    62  # Shift Right
    37  # Ctrl Left
    115 # Logo Left (-> Option)
    64  # Alt Left (-> Command)
    110 # Alt Right
    117 # Menu (-> International)
    109 # Ctrl Right
    50 # Shift Left
  )
  for keycode in "${keys_to_turn_off_repeat[@]}"
  do
    xset -r "$keycode"
  done
fi

# Export settings that need to be passed to whist user when running as whist user
export SENTRY_ENVIRONMENT=${SENTRY_ENVIRONMENT:-}
export SYSTEM_LANGUAGES=$SYSTEM_LANGUAGES
export CLIENT_OS=$CLIENT_OS
export KIOSK_MODE=$KIOSK_MODE
export LOAD_EXTENSION=$LOAD_EXTENSION

exec runuser --login whist --whitelist-environment=LOAD_EXTENSION,KIOSK_MODE,SENTRY_ENVIRONMENT,SYSTEM_LANGUAGES,CLIENT_OS -c \
  'DISPLAY=:10 \
    LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/usr/lib/i386-linux-gnu:/usr/local/nvidia/lib:/usr/local/nvidia/lib64 \
    LOCAL=yes \
    LC_ALL=C \
    PATH=/usr/local/cuda-11.0/bin:/usr/local/nvidia/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
  DEBIAN_FRONTEND=noninteractive '"$1"
