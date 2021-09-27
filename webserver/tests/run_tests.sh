#!/bin/bash

# exit on error and missing env var
set -Eeuo pipefail

# Retrieve relative subfolder path
# https://stackoverflow.com/questions/59895/how-to-get-the-source-directory-of-a-bash-script-from-within-the-script-itself
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
# Make sure we are always running this script with working directory `webserver/tests`
cd "$DIR"

# if in CI, run setup tests and set env vars
IN_CI=${CI:=false} # default: false
if [ $IN_CI == true ]; then
  # these are needed to migrate schema/data
  export POSTGRES_SOURCE_URI=$DATABASE_URL # set in config vars on Heroku
  export POSTGRES_DEST_URI=$POSTGRES_EPHEMERAL_DB_URL # set in app.json, _URL appended by Heroku
  export DB_EXISTS=true # Heroku has created the db
  # this sets up the local db to look like the remote db
  bash setup/setup_tests.sh
  # override DATABASE_URL to the ephemeral db
  export DATABASE_URL=$POSTGRES_DEST_URI
else
  echo "=== Make sure to run tests/setup/setup_tests.sh once prior to this ==="

  # add env vars to current env. these tell us the host, db, role, pwd
  export $(cat ../docker/.env | xargs)

  # override POSTGRES_HOST and POSTGRES_PORT to be local
  export POSTGRES_HOST="localhost"
  export POSTGRES_PORT="9999"

  export APP_GIT_BRANCH="$(git branch --show-current)"
  export APP_GIT_COMMIT="$(git rev-parse --short HEAD)"

  # we use the remote user and remote db to make ephemeral db look as close to dev as possible
  # but of course, host and port are local
  export DATABASE_URL=postgres://${POSTGRES_USER}@${POSTGRES_HOST}:${POSTGRES_PORT}/${POSTGRES_DB}
fi

# regardless of in CI or local tests, we set these variables
export TESTING=true

# Only set Codecov flags if running in CI
cov="$(test -z "${COV-}" -a "$IN_CI" = "false" || echo "--cov-report xml --cov=app/ --cov=auth0 --cov=payments")"

# pass args to pytest, including Codecov flags for relevant webserver folders, and ignore the scripts/ 
# folder as it's irrelevant to unit/integration testing
(cd .. && pytest --ignore=scripts $cov "$@")

# Download the Codecov uploader
curl -Os https://uploader.codecov.io/latest/linux/codecov && chmod +x codecov

# Upload the Codecov XML coverage report to Codecov, using the environment variable CODECOV_TOKEN
# stored as a Heroku config variable
# -R is to specify the project root folder, necessary since we move only the /app folder to Heroku CI
# -S is to specify the commit sha, necessary since Codecov can't detect GHA due to using Heroku CI
test "$IN_CI" = "false" || (./codecov -R /app --sha ${APP_GIT_COMMIT} -t ${CODECOV_TOKEN} -c -F webserver)
