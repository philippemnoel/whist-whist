#!/bin/bash

# Used as the entrypoint by Dockerfile to determine if the instance
# should be running as `web` or `celery`.

# The web and celery services in our docker-compose stack are so similar that
# they share the same Dockerfile. The web service only differs from the celery
# service because the commands specified for each service in
# docker/docker-compose.yml, is different. This file is the entrypoint for
# both containers. See https://docs.docker.com/engine/reference/builder/#cmd to
# learn about the interaction between Dockerfile entrypoints and commands.

# This file is named "stem-cell.sh" because just as stem cells can grow up to
# become any kind of cell, this file can execute either a web or celery process
# depending on its argument.

# TODO use this as the entrypoint for the Procfile as well, e.g.
# Procfile
#   web: ./stem-cell.sh web
#   celery: ./stem-cell.sh celery
#
# This will ensure increased uniformity between dev and production
# environments.

# Exit on subcommand errors
set -Eeuo pipefail

case "$1" in
    "web")
        waitress-serve --port="$PORT" --url-scheme=https entry_web:app ;;
    "celery")
        # The two containers share the same requirements.txt file, but we only
        # want the watchmedo utility to be installed in the Celery container.
        supervisord -c supervisor.conf ;;
    *) echo "Specify either 'web' or 'celery' to determine what this" \
            "instance will manifest as." ;;
esac
