#!/bin/bash

# ${1} the webserver url
# ${2} admin token
# ${3} the type, must be "start" or "end"

set -Eeuo pipefail

WEBSERVER_URL=${1}
ADMIN_TOKEN=${2}
TYPE=${3}

if [ $TYPE != "start" ] && [ $TYPE != "end" ]; then
    echo "Third argument was $TYPE. Must be 'start' or 'end'"
    exit 1
fi

# 450 iterations * 2 sec sleep = 900 sec = 15 minutes (not counting CURL time)
# that should be enough time for tasks to stop
for try in {0..450}
do
    echo "Try $try: $TYPE maintenance..."
    resp=$(curl -H "Authorization: Bearer $ADMIN_TOKEN" -X POST "$WEBSERVER_URL/aws_container/${TYPE}_update")
    success=$(echo $resp | jq -r ".success")
    if [ $success == "true" ]; then
        exit 0
    else
        sleep 2
    fi
done

exit 1

