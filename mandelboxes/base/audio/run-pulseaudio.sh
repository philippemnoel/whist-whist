#!/bin/bash

# # Enable Sentry bash error handler, this will catch errors if `set -e` is set in a Bash script
SENTRY_ENV_FILENAME=/usr/share/whist/private/sentry_env
case $(cat $SENTRY_ENV_FILENAME) in
  dev|staging|prod)
    export SENTRY_ENVIRONMENT=${SENTRY_ENV}
    eval "$(sentry-cli bash-hook)"
    ;;
  *)
    echo "Sentry environment not set, skipping Sentry error handler"
    ;;
esac

# # Exit on subcommand errors
set -Eeuo pipefail

# Wait for the PulseAudio unix socket to be active
# Uses 5% CPU until pulseaudio activates
while ! pactl list | grep UNIX; do
  sleep 0.01
done

pulseaudio_watchdog() {
  # Periodically check that the pulseaudio daemon exists
  while pidof pulseaudio > /dev/null; do
    sleep 10
  done

  exit 1
}

# Run the watchdog
pulseaudio_watchdog &

# Write the pid of the watchdog into the pidfile for the systemd service
echo "$!" > /usr/share/whist/whist-audio-pid

# Detatch the watchdog
disown

# Exit, to tell systemd that all is well
