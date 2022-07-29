#!/bin/bash

# This script starts the symlinked whist-application as the whist user.

# Enable Sentry bash error handler, this will catch errors if `set -e` is set in a Bash script
# This is called via `./run-as-whist-user.sh`, which passes sentry environment in.
case $SENTRY_ENVIRONMENT in
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

# Write the PID to a file
WHIST_APPLICATION_PID_FILE=/home/whist/whist-application-pid
echo $$ > $WHIST_APPLICATION_PID_FILE

# Wait for the PID file to have been removed
block-while-file-exists.sh $WHIST_APPLICATION_PID_FILE >&1

# Pass JSON transport settings as environment variables
export DARK_MODE=$DARK_MODE
export RESTORE_LAST_SESSION=$RESTORE_LAST_SESSION
export TZ=$TZ # TZ variable automatically adjusts the timezone (https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html)
export INITIAL_URL=$INITIAL_URL
export USER_AGENT=$USER_AGENT
export KIOSK_MODE=$KIOSK_MODE
export LONGITUDE=$LONGITUDE
export LATITUDE=$LATITUDE
# Set the locale (the line below exports all the locale environment variables)
export ${USER_LOCALE?}
export LANGUAGE=$SYSTEM_LANGUAGES
export BROWSER_LANGUAGES=$BROWSER_LANGUAGES
export CLIENT_OS=$CLIENT_OS

# Explicitly export the fonts path, so that the
# application can find the fonts. Per: https://askubuntu.com/questions/492033/fontconfig-error-cannot-load-default-config-file
export FONTCONFIG_PATH=/etc/fonts

if [[ -z ${WHIST_DEST_BROWSER+1} ]]; then
  echo "WHIST_DEST_BROWSER is not set! Defaulting to Chrome"
  WHIST_DEST_BROWSER="chrome"
fi

WHIST_MAPPINGS_DIR=/whist/resourceMappings
SESSION_ID_FILENAME=$WHIST_MAPPINGS_DIR/session_id
WHIST_LOGS_FOLDER=/var/log/whist
APPLICATION_OUT_FILENAME=$WHIST_LOGS_FOLDER/whist_application-out.log
APPLICATION_ERR_FILENAME=$WHIST_LOGS_FOLDER/whist_application-err.log

# Read the session id, if the file exists
if [ -f "$SESSION_ID_FILENAME" ]; then
  SESSION_ID=$(cat $SESSION_ID_FILENAME)
  APPLICATION_OUT_FILENAME=$WHIST_LOGS_FOLDER/$SESSION_ID/whist_application-out.log
  APPLICATION_ERR_FILENAME=$WHIST_LOGS_FOLDER/$SESSION_ID/whist_application-err.log
fi

# Start the application that this mandelbox runs
exec whist-application $WHIST_DEST_BROWSER > $APPLICATION_OUT_FILENAME 2>$APPLICATION_ERR_FILENAME
