#!/bin/bash

# This script is the first script run within the Whist mandelbox. It retrieves the
# mandelbox-specific parameters from Linux environment variables and sets them for
# the Whist mandelbox to use. It then starts systemd, which starts all of the
# Whist system services (.service files), starting Whist inside the mandelbox.

SENTRY_ENV_FILENAME=/usr/share/whist/private/sentry_env
# If SENTRY_ENV is set, then create file
if [ -n "${SENTRY_ENV+1}" ]
then
  echo "$SENTRY_ENV" > $SENTRY_ENV_FILENAME
fi

# Enable Sentry bash error handler, this will `set -e` and catch errors in a bash script
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

WHIST_PRIVATE_DIR=/usr/share/whist/private

# If WHIST_AES_KEY is set, then create file
if [ -n "${WHIST_AES_KEY+1}" ]
then
  echo "$WHIST_AES_KEY" > $WHIST_PRIVATE_DIR/aes_key
fi

# Unset the AWS key to make sure that this environment variable does not
# leak in any way (probably redundant, but still good practice)
unset WHIST_AES_KEY

# Set dev env vars
if [ -n "${KIOSK_MODE+1}" ]
then
  echo "$KIOSK_MODE" > $WHIST_PRIVATE_DIR/kiosk_mode
fi
if [ -n "${LOAD_EXTENSION+1}" ]
then
  echo "$LOAD_EXTENSION" > $WHIST_PRIVATE_DIR/load_extension
fi
if [ -n "${LOCAL_CLIENT+1}" ]
then
  echo "$LOCAL_CLIENT" > $WHIST_PRIVATE_DIR/local_client
fi

# Remove a vestigal file that we do not use.
# This is how LXC used to read environment variables: see that deprecated code in
# https://github.com/moby/moby/blob/v1.9.1/daemon/execdriver/lxc/init.go#L107-L134
# We remove the file to make our infrastructure less obvious.
rm /.dockerenv

# Start systemd
exec /lib/systemd/systemd
