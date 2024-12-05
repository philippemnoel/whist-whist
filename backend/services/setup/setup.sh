#!/bin/bash

set -Eeuo pipefail

# Setup local database and Hasura servers for local scaling service testing.

function usage() {
  cat <<USAGE

    Usage: $0 [--config]

    Options:
        --config:  Setup the configuration database for local testing and development. Useful for services that
                   interact with the config db. Note that this requires login to a Heroku account.
USAGE
  exit 1
}

USE_CONFIG_DB=false

for arg in "$@"; do
  case $arg in
    --config)
      USE_CONFIG_DB=true
      ;;
    -h | --help)
      usage
      ;;
    *)
      usage
      exit 1
      ;;
  esac
  shift
done

# add_database_hasura will add the database given by the arguments
# to Hasura as a data source.
# Args:
# $1: Hasura metadata URL, accesible locally
# $2: Database connection string, inside Docker network
add_database_hasura() {
  curl -X POST "$1" \
    -H "Content-Type: application/json" \
    --data-binary @- << EOF
{
  "type": "pg_add_source",
  "args": {
    "name": "Whist Local",
    "configuration": {
      "connection_info": {
        "database_url": "$2",
        "pool_settings": {
          "max_connections": 50,
          "idle_timeout": 180,
          "retries": 1,
          "pool_timeout": 360,
          "connection_lifetime": 600
        },
        "use_prepared_statements": true,
        "isolation_level": "read-committed"
      }
    },
    "replace_configuration": false
  }
}
EOF
}

# wait_database_ready tries to connect to the database
# using psql, or retries if it's not available.
# Args:
# $1: Postgres connection string
wait_database_ready() {
  cmds="\q"
  while ! (docker run --rm --network=setup_default -i postgres psql "$1" <<< $cmds) &> /dev/null
  do
    echo "Connection failed. Retrying in 2 seconds..."
    sleep 2
  done
}


# Allow passing `--down` to spin down the docker-compose stack, instead of
# having to cd into this directory and manually run the command.
if [[ $* =~ [:space:]*--down[:space:]* ]]; then
  echo "Running \"docker-compose down\"."
  docker-compose down
  exit 0
fi

# PostgreSQL strings used for connecting to the databse on the host machine.
LOCAL_WHIST_PGSTRING="postgresql://postgres:whistpass@postgres/postgres"
LOCAL_CONFIG_PGSTRING="postgresql://postgres:whistpass@postgres-config/postgres"

# PostgreSQL strings used for connecting to the database inside the Docker network.
DOCKER_WHIST_PGSTRING="postgres://postgres:whistpass@postgres:5432/postgres"
DOCKER_CONFIG_PGSTRING="postgres://postgres:whistpass@postgres-config:5432/postgres"

# URLs to make calls to the Hasura API.
LOCAL_WHIST_URL="http://localhost:8080/v1/metadata"
LOCAL_CONFIG_URL="http://localhost:8082/v1/metadata"

# Start Hasura and Postgres databases.
docker-compose up -d

echo "Waiting for databases to be ready..."
wait_database_ready "$LOCAL_WHIST_PGSTRING" &
wait_database_ready "$LOCAL_CONFIG_PGSTRING" &
wait

echo "Setting up local databases..."
docker run --rm --network=setup_default -i postgres psql "$LOCAL_WHIST_PGSTRING" < ../../database/schema.sql

if [[ $USE_CONFIG_DB == true ]]; then
  # First pull the config schema because its not tracked in the codebase.
  echo "Pulling config database schema..."
  DEV_CONFIG_DATABASE=$(heroku config:get HEROKU_POSTGRESQL_MAROON_URL -a whist-dev-scaling-service)
  docker run --rm postgres pg_dump --no-owner --no-privileges --schema-only "$DEV_CONFIG_DATABASE" > config_schema.sql
  docker run --rm --network=setup_default -i postgres psql "$LOCAL_CONFIG_PGSTRING" < config_schema.sql
  echo "Populating config database..."
  docker run --rm --network=setup_default -i postgres psql "$LOCAL_CONFIG_PGSTRING" <<EOF
\x
INSERT INTO dev VALUES
    ('DESIRED_FREE_MANDELBOXES_US_EAST_1', '2'),
    ('ENABLED_REGIONS', '["us-east-1"]'),
    ('MANDELBOX_LIMIT_PER_USER', '3');

INSERT INTO desktop_app_version(id, major, minor, micro, dev_rc, staging_rc, dev_commit_hash, staging_commit_hash, prod_commit_hash)
    VALUES
        (1, 3, 0, 0, 0, 0, 'dummy_commit_hash', 'dummy_commit_hash', 'dummy_commit_hash');
EOF
  echo "Cleaning up..."
  rm config_schema.sql
fi

echo "Adding databases to Hasura servers..."
add_database_hasura "$LOCAL_WHIST_URL" "$DOCKER_WHIST_PGSTRING"
add_database_hasura "$LOCAL_CONFIG_URL" "$DOCKER_CONFIG_PGSTRING"

echo ""

green="\e[0;92m"
reset="\e[0m"

echo -e "${green}Your machine is ready to run the backend services! You can run make run_host_service_localdevwithdb or make run_scaling_service_localdevwithdb now to start developing."
echo "Make sure to visit http://localhost:8080 and http://localhost:8082 to track the tables/relationships in the Hasura console."
echo -e "In the future, simply run docker-compose up and the Hasura servers will be ready to use.${reset}"
