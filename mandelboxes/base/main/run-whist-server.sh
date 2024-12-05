#!/bin/bash

# This script starts the Whist protocol server and the Whist application.

# Enable Sentry bash error handler, this will catch errors if `set -e` is set in a Bash script
WHIST_PRIVATE_DIR=/usr/share/whist/private
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

# Set/Retrieve Mandelbox parameters
WHIST_MAPPINGS_DIR=/whist/resourceMappings/
WHIST_LOGS_FOLDER=/var/log/whist
IDENTIFIER_FILENAME=hostPort_for_my_32262_tcp
PRIVATE_KEY_FILENAME=$WHIST_PRIVATE_DIR/aes_key
CONNECT_TIMEOUT_FILENAME=$WHIST_MAPPINGS_DIR/connect_timeout
DISCONNECT_TIMEOUT_FILENAME=$WHIST_MAPPINGS_DIR/disconnect_timeout

WHIST_APPLICATION_PID_FILE=/home/whist/whist-application-pid

# To avoid interfering with Filebeat, the logs files should not contain hyphens in the name before the {-out, -err}.log suffix
PROTOCOL_OUT_FILENAME=$WHIST_LOGS_FOLDER/protocol_server-out.log
PROTOCOL_ERR_FILENAME=$WHIST_LOGS_FOLDER/protocol_server-err.log
TELEPORT_OUT_FILENAME=$WHIST_LOGS_FOLDER/teleport_drag_drop-out.log
TELEPORT_ERR_FILENAME=$WHIST_LOGS_FOLDER/teleport_drag_drop-err.log

# TODO: set to development flag
WHIST_PRIVATE_DIR=/usr/share/whist/private
LOCAL_CLIENT_FILENAME=$WHIST_PRIVATE_DIR/local_client
LOCAL_CLIENT=false # true if the frontend is being tested manually by a Whist engineer
# Send in LOAD_EXTENSION and KIOSK_MODE, if set
if [ -f "$LOCAL_CLIENT_FILENAME" ]; then
  LOCAL_CLIENT=$(cat $LOCAL_CLIENT_FILENAME)
fi

# Define a string-format identifier for this mandelbox
IDENTIFIER=$(cat $WHIST_MAPPINGS_DIR/$IDENTIFIER_FILENAME)

# Create list of command-line arguments to pass to the Whist protocol server
OPTIONS=""

# Send in AES private key, if set
if [ -f "$PRIVATE_KEY_FILENAME" ]; then
  WHIST_AES_KEY=$(cat $PRIVATE_KEY_FILENAME)
  export WHIST_AES_KEY
  OPTIONS="$OPTIONS --private-key=$WHIST_AES_KEY"
fi

# Send in Sentry environment, if set, except for the LOCAL_CLIENT case,
# since local clients might be in a version mismatch with server protocol
if [ -f "$SENTRY_ENV_FILENAME" ] && [ "$LOCAL_CLIENT" == "false" ]; then
  SENTRY_ENV=$(cat $SENTRY_ENV_FILENAME)
  export SENTRY_ENV
  OPTIONS="$OPTIONS --environment=$SENTRY_ENV"
fi

# Send in connection timeout, if set
if [ -f "$CONNECT_TIMEOUT_FILENAME" ]; then
  CONNECT_TIMEOUT=$(cat $CONNECT_TIMEOUT_FILENAME)
  export CONNECT_TIMEOUT
  OPTIONS="$OPTIONS --connect-timeout=$CONNECT_TIMEOUT"
fi

# Send in disconnection timeout, if set
if [ -f "$DISCONNECT_TIMEOUT_FILENAME" ]; then
  DISCONNECT_TIMEOUT=$(cat $DISCONNECT_TIMEOUT_FILENAME)
  export DISCONNECT_TIMEOUT
  OPTIONS="$OPTIONS --disconnect-timeout=$DISCONNECT_TIMEOUT"
fi

# We use named pipe redirection for consistency with our WhistServer launch setup
# &> redirects both stdout and stdin together; shorthand for '> XYZ 2>&1'
/usr/share/whist/run-as-whist-user.sh "/usr/bin/run-whist-teleport-drag-drop.sh" > $TELEPORT_OUT_FILENAME 2>$TELEPORT_ERR_FILENAME &

# This function is called whenever the script exits, whether that is because we
# reach the end of this file (because either WhistServer or the Whist
# application died), or there was an error somewhere else in the script.
function cleanup {
  echo "cleanup() called! Shutting down the mandelbox..."

  # Make sure we shutdown the mandelbox when this script exits
  sudo shutdown now
}

# TODO: this needs to be adjusted since we'll have -1 in prod too
if [ "$CONNECT_TIMEOUT" != "-1" || "$DISCONNECT_TIMEOUT" != "-1" ]; then
  # Make sure `cleanup` gets called on script exit except when TIMEOUT is -1, which
  # only occurs during local development. This timeout-setting logic is controlled
  # by spinup.go in the host-service.
  trap cleanup EXIT ERR
fi

# WhistServer and cookie authentication require that the D-Bus environment variables be set
# The -10 comes from the display ID
dbus_env_file="/home/whist/.dbus/session-bus/$(cat /etc/machine-id)-10"
# shellcheck source=/dev/null
. "$dbus_env_file"
export DBUS_SESSION_BUS_ADDRESS
echo "loaded d-bus address in run-whist-server.sh: $DBUS_SESSION_BUS_ADDRESS"

# Add D-Bus address to options
OPTIONS="$OPTIONS --dbus-address=$DBUS_SESSION_BUS_ADDRESS"

WHIST_USER_LOGS_FOLDER=/home/whist
# To avoid interfering with Filebeat, the logs files should not contain hyphens in the name before the {-out, -err}.log suffix
APPLICATION_OUT_FILENAME=whist_application-out.log
APPLICATION_ERR_FILENAME=whist_application-err.log
ln -s $WHIST_USER_LOGS_FOLDER/$APPLICATION_OUT_FILENAME $WHIST_LOGS_FOLDER/$APPLICATION_OUT_FILENAME
ln -s $WHIST_USER_LOGS_FOLDER/$APPLICATION_ERR_FILENAME $WHIST_LOGS_FOLDER/$APPLICATION_ERR_FILENAME

ENABLE_GPU_COMMAND_STREAMING=0

start_browser() {
  # Start the application that this mandelbox runs.
  /usr/share/whist/run-as-whist-user.sh "/usr/bin/run-whist-application.sh $ENABLE_GPU_COMMAND_STREAMING" &
  whist_application_runuser_pid=$!

  echo "Whist application runuser pid: $whist_application_runuser_pid"

  # Wait for run-whist-application.sh to write PID to file
  block-until-file-exists.sh $WHIST_APPLICATION_PID_FILE >&1
  whist_application_pid=$(cat $WHIST_APPLICATION_PID_FILE)
  rm $WHIST_APPLICATION_PID_FILE

  echo "Whist application pid: $whist_application_pid"
}

# Start the browser before WhistServer only when GPU command streaming is disabled
if [ "$ENABLE_GPU_COMMAND_STREAMING" == 0 ]; then
  echo "Starting the browser before WhistServer"

  start_browser
  echo "Now sleeping until there are X clients..."
  # Wait until the application has created its display before launching WhistServer.
  # This prevents a black no input window from appearing when a user connects.
  until [ $(xlsclients -display :10 | wc -l) != 0 ]
  do
    sleep 0.1
  done
  echo "Done sleeping until there are X clients..."
  echo "done" > $WHIST_MAPPINGS_DIR/done_sleeping_until_X_clients
else
  OPTIONS="$OPTIONS --enable-gpu-command-streaming"
fi
sync # Necessary so that even if the container exits very soon the host service sees the file written.

# Send in identifier
OPTIONS="$OPTIONS --identifier=$IDENTIFIER"

/usr/share/whist/WhistServer $OPTIONS > $PROTOCOL_OUT_FILENAME 2>$PROTOCOL_ERR_FILENAME &
whist_server_pid=$!


if [ "$ENABLE_GPU_COMMAND_STREAMING" == 1 ]; then
  echo "Starting the browser after WhistServer"
  start_browser
fi

# Wait for either whist-application or WhistServer to exit (both backgrounded processes).

# TODO: once our mandelboxes have bash 5.1 we will be able to deduce _which_
# application exited with the `-p` flag to `wait`.
wait -n
echo "Either WhistServer or whist-application exited with code $?"
echo "WhistServer PID: $whist_server_pid"
echo "runuser whist-application PID: $whist_application_runuser_pid"
echo "whist-application PID: $whist_application_pid"
echo "Remaining job PIDs: $(jobs -p)"

# Kill whatever is still running of WhistServer and whist-application, with SIGTERM.
kill "$whist_application_pid" ||:
kill $whist_server_pid ||:

# Wait for whist-application to finish terminating, ignoring exit code
echo "Waiting for Whist Application Runner to finish"
wait $whist_application_runuser_pid ||:

# Wait for WhistServer to gracefully exit
echo "Waiting on WhistServer to exit"
wait $whist_server_pid ||:

echo "Both whist-application and WhistServer have exited."

# We now pass control over to `cleanup`, since we've reached the end of the script.
