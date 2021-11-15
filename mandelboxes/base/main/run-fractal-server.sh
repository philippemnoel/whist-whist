#!/bin/bash

# This script starts the Whist protocol server and the Whist application.

# Exit on subcommand errors
set -Eeuo pipefail

# Set/Retrieve Mandelbox parameters
FRACTAL_MAPPINGS_DIR=/fractal/resourceMappings
IDENTIFIER_FILENAME=hostPort_for_my_32262_tcp
PRIVATE_KEY_FILENAME=/usr/share/fractal/private/aes_key
SENTRY_ENV_FILENAME=/usr/share/fractal/private/sentry_env
COOKIE_FILE_FILENAME=/usr/share/fractal/private/user_cookies_file
USER_UPLOAD_TARGET_FILENAME=/usr/share/fractal/private/user_target
TIMEOUT_FILENAME=$FRACTAL_MAPPINGS_DIR/timeout
FRACTAL_APPLICATION_PID_FILE=/home/fractal/fractal-application-pid
PROTOCOL_LOG_FILENAME=/usr/share/fractal/server.log
TELEPORT_LOG_FILENAME=/usr/share/fractal/teleport.log


# Define a string-format identifier for this mandelbox
IDENTIFIER=$(cat $FRACTAL_MAPPINGS_DIR/$IDENTIFIER_FILENAME)

# Create list of command-line arguments to pass to the Whist protocol server
OPTIONS=""

# Send in AES private key, if set
if [ -f "$PRIVATE_KEY_FILENAME" ]; then
  export FRACTAL_AES_KEY=$(cat $PRIVATE_KEY_FILENAME)
  OPTIONS="$OPTIONS --private-key=$FRACTAL_AES_KEY"
fi

# Send in Sentry environment, if set
if [ -f "$SENTRY_ENV_FILENAME" ]; then
  export SENTRY_ENV=$(cat $SENTRY_ENV_FILENAME)
  OPTIONS="$OPTIONS --environment=$SENTRY_ENV"
fi

# Send in timeout, if set
if [ -f "$TIMEOUT_FILENAME" ]; then
  export TIMEOUT=$(cat $TIMEOUT_FILENAME)
  OPTIONS="$OPTIONS --timeout=$TIMEOUT"
fi

# Set cookies file, if file exists
if [ -f "$COOKIE_FILE_FILENAME" ]; then
  export WHIST_INITIAL_USER_COOKIES_FILE=$(cat $COOKIE_FILE_FILENAME)
fi

# Set user upload target, if file exists
if [ -f "$USER_UPLOAD_TARGET_FILENAME" ]; then
  export WHIST_COOKIE_UPLOAD_TARGET=$(cat $USER_UPLOAD_TARGET_FILENAME)
fi


# We use named pipe redirection for consistency with our FractalServer launch setup
# &> redirects both stdout and stdin together; shorthand for '> XYZ 2>&1'
/usr/share/fractal/run-as-fractal-user.sh "/usr/bin/run-fractal-teleport.sh" &> >(tee $TELEPORT_LOG_FILENAME) &

# This function is called whenever the script exits, whether that is because we
# reach the end of this file (because either FractalServer or the Whist
# application died), or there was an error somewhere else in the script.
function cleanup {
  echo "cleanup() called! Shutting down the mandelbox..."

  # Make sure we shutdown the mandelbox when this script exits
  sudo shutdown now
}

export ENV_NAME=$(cat $SENTRY_ENV_FILENAME)
if [ "$ENV_NAME" != "LOCALDEV" ]; then
  # Make sure `cleanup` gets called on script exit in all environments except localdev.
  trap cleanup EXIT ERR
fi

# Imports user cookies
/usr/share/fractal/run-as-fractal-user.sh "python3 /usr/bin/import_custom_cookies.py"

# Start the application that this mandelbox runs.
/usr/share/fractal/run-as-fractal-user.sh "/usr/bin/run-fractal-application.sh" &
fractal_application_runuser_pid=$!

echo "Whist application runuser pid: $fractal_application_runuser_pid"

# Wait for run-fractal-application.sh to write PID to file
until [ -f "$FRACTAL_APPLICATION_PID_FILE" ]
do
  sleep 0.1
done
fractal_application_pid=$(cat $FRACTAL_APPLICATION_PID_FILE)
rm $FRACTAL_APPLICATION_PID_FILE

echo "Whist application pid: $fractal_application_pid"

echo "Now sleeping until there are X clients..."

# Wait until the application has created its display before launching FractalServer.
#    This prevents a black no input window from appearing when a user connects.
until [ $(xlsclients -display :10 | wc -l) != 0 ]
do
  sleep 0.1
done

echo "Done sleeping until there are X clients..."
echo "done" > $FRACTAL_MAPPINGS_DIR/done_sleeping_until_X_clients
sync # Necessary so that even if the container exits very soon the host service sees the file written.

# Send in identifier
OPTIONS="$OPTIONS --identifier=$IDENTIFIER"

# The point of the named pipe redirection is so that $! will give us the PID of FractalServer, not of tee.
/usr/share/fractal/FractalServer $OPTIONS > >(tee $PROTOCOL_LOG_FILENAME) &
fractal_server_pid=$!

# Wait for either fractal-application or FractalServer to exit (both backgrounded processes).

# TODO: once our mandelboxes have bash 5.1 we will be able to deduce _which_
# application exited with the `-p` flag to `wait`.
wait -n
echo "Either FractalServer or fractal-application exited with code $?"
echo "FractalServer PID: $fractal_server_pid"
echo "runuser fractal-application PID: $fractal_application_runuser_pid"
echo "fractal-application PID: $fractal_application_pid"
echo "Remaining job PIDs: $(jobs -p)"

# Kill whatever is still running of FractalServer and fractal-application, with SIGTERM.
kill $fractal_application_pid ||:
kill $fractal_server_pid ||:

# Wait for fractal-application to finish terminating, ignoring exit code (since
wait $fractal_application_runuser_pid ||:

echo "Both fractal-application and FractalServer have exited."

# We now pass control over to `cleanup`, since we've reached the end of the script.
