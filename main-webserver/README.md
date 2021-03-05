# Fractal Main Webserver

This repository contains the code for our webserver, which is our REST API and provides backend support for our user interfaces, our internal tools, and our container/virtual machine management. This README contains general set-up information&mdash;for a high-level understanding of the code, read each of the READMEs in the the subdirectories here (`/app`, `/docker`, etc.)

Our webservers and CD pipeline are hosted on Heroku. Our production database is attached as an Heroku Add-On PostgresSQL to the associated webserver in Heroku, `main-webserver`, and has automated backups in place daily at 2 AM PST. See [here](https://devcenter.heroku.com/articles/heroku-postgres-backups#creating-a-backup) for further information.

Our webserver logs are hosted on Datadog [here](https://app.datadoghq.com/logs?cols=core_host%2Ccore_service&from_ts=1593977274176&index=&live=true&messageDisplay=inline&stream_sort=desc&to_ts=1593978174176).

#### Coding Philosophy

Before contributing to this project, please read our in-depth coding philosophy document [here](https://www.notion.so/tryfractal/Code-Philosophy-Webserver-backend-d036205444464f8b8a61dc36eeae7dbb).

## Development

### Getting Started

#### Local Setup

Right now, in order to be _sure_ that changes you make to the web stack won't break anything when deployed, we strongly advise locally building any changes you make -- particularly to the app startup section of the code (create_app, register_blueprints, and init_celery). Our CI covers the rest of the pipeline (so you can be relatively confident changes elsewhere won't set everything on fire) but even then the best way to test code is still with a local build. Instructions for that build can be found below.

The web application stack is comprised of three main components:

- The web server itself, which is written in Python using the [Flask](https://flask.palletsprojects.com/en/1.1.x/) web framework.
- An asynchronous task queue, which is a Redis-backed [Celery](https://docs.celeryproject.org/en/stable/index.html) task queue. As such, the task queue can be broken down into two more granular sub-components: a pool of worker processes, and a Redis store.
- A database. The database is a Postgres instance that is shared by multiple developers.

We use [`docker-compose`](https://docs.docker.com/compose/) to spin part of the web server stack up (the `docker-compose` stack does not include the Postgres database, which is shared between multiple developers and app deployments, as mentioned above) locally for development purposes. `docker-compose` builds Docker images for the Flask server and the Celery worker pool and deploys them alongside containerized Redis. There is also a `pytest` test suite that developers may run locally. None of these commands are run directly, and are instead wrapped by bash scripts that do a bit of preparation (namely `docker/local_deploy.sh` and `tests/setup/setup_tests.sh`).

The following environment variables must also be set in `docker/.env` (neither the test suite nor the `docker-compose` stack will work without them).

- `POSTGRES_DB` &ndash; The name of the Postgres database to which to connect.
- `POSTGRES_HOST` &ndash; The hostname or IP address of the development Postgres instance.
- `POSTGRES_PASSWORD` &ndash; The password used to authenticate with the local stack's PostgresQL instance.
- `POSTGRES_USER` &ndash; The name of the user as whom to log into the development Postgres instance.

All of local deployment, local testing, and CI use ephemeral DBs that are mostly empty copies of the dev database. The copying script (see `db_setup/`) looks at the database specified by those environment variables.

If you'd like to retrieve more information from the dev db (to put in the ephemeral DBs) than the tables we currently have, open up `db_setup/fetch_db.sh` and find the _two_ lines with:

```
(pg_dump -h $POSTGRES_REMOTE_HOST -U $POSTGRES_REMOTE_USER -d $POSTGRES_REMOTE_DB --data-only --column-inserts ... ) > db_data.sql
```

For each table you'd like to fetch from dev, add `-t schema_name.table_name` to `...`. Remember to delete the existing `.sql` scripts and rerun any scripts afterwards.

**1. Set environment variables**

Luckily, there is an easy way to set all of the necessary environment variables using the script `docker/retrieve_config.sh`. To set all of the required environment variables, first make sure you have [Heroku CLI](https://devcenter.heroku.com/articles/heroku-cli) installed and configured. Then, run

    bash docker/retrieve_config.sh

When the `docker/retrieve_config.sh` script terminates, it will print the name of the file containing the fetched environment variables that has been written to standard error.

**2. Set AWS credentials**

Whether you're running tests or the `docker-compose` stack locally, the web server needs to be able to access AWS APIs. You can set your AWS credentials using either the same files in the `~/.aws/` directory or environment variables that you can use to configure [`boto`](https://boto3.amazonaws.com/v1/documentation/api/latest/guide/credentials.html) and the [AWS CLI](https://docs.aws.amazon.com/cli/latest/userguide/cli-chap-configure.html). Environment variables set in `docker/.env` will be loaded by `pytest` during test run sessions and set in the execution environment within the containers in the docker-compose stack.

> I recommend downloading the AWS CLI, which you may find useful in the future, and running `aws configure`.
>
> -O

**3. Configure OAuth client (optional)**

Before you can create new containers with cloud storage folders mounted to them from an instance of the web server running locally, you must download the [Google client secret file](https://s3.console.aws.amazon.com/s3/object/fractal-dev-secrets?region=us-east-1&prefix=client_secret.json) from S3. Save it to `main-webserver/client_secret.json`. An easy way to do this is to run:

    aws s3 cp --only-show-errors s3://fractal-dev-secrets/client_secret.json client_secret.json

from within the `main-webserver` directory.

This step is optional. If you choose not to complete this step, you will still be able to launch containers, but those containers will not have cloud storage access, even for accounts to which a cloud storage provider is connected.

**4. Spin Up Local Servers**

Run the following to do a local deployment. If you are on Windows, you should run this from a command prompt in Administrator mode. This will create dummy SSL certificates (to get as close to the Redis+TLS setup we have in production) and start the app at `run.py`.

```sh
bash docker/local_deploy.sh
```

If you encounter a "daemon not running" error, this likely means that Docker is not actually running. To fix this, try restarting your computer or opening the Docker desktop app; if the app opens successfully, then the issue should go away. You can optionally pass the argument `--use-dev-db`. Only do this if you absolutely need the dev db. Generally speaking, you should be able to recreate any resource on dev dbs in your ephemeral db. If you do this, please explain in `#webserver` why the ephemeral db did not meet your needs so we can improve it.

Review `docker/docker-compose.yml` to see which ports the various services are hosted on. For example, `"7810:6379"` means that the Redis service, running on port 6379 internally, will be available on `localhost:7810` from the host machine. Line 25 of `docker-compose.yml` will tell you where the webserver itself is running. When you're done, end the containers with `bash docker/local_deploy.sh --down`.

By default, hot-reloading of the Flask web server and Celery task queue is disabled (`HOT_RELOAD=`). To enable it, set `HOT_RELOAD` to a non-empty string in `docker/local_deploy.sh`.

### Flask CLI

Take advantage of the Flask CLI! Run `flask --help` for information about what commands are available. If, at any point, there is a script that you need to write that is useful for modifying the Flask application or any of its resources (e.g. databases), you are encourage to write it as a Flask CLI command. Start reading about the Flask CLI documentation [here](https://flask.palletsprojects.com/en/1.1.x/cli/?highlight=cli#custom-commands).

### Helper Software Setup

We recommend that you download several softwares to help you code and test:

**Postman**

We use Postman to send API requests to our server, to store our API endpoints, and to generate documentation. Our Postman team link is [here](https://tryfractal.postman.co/). If you are not part of the team, contact @mingy98. To better understand how Postman works, refer to our wiki [here](https://www.notion.so/tryfractal/Postman-API-Documentation-602cc6df23e04cd0a026340c406bd663).

**TablePlus**

We use TablePlus to visualize, search, and modify our SQL database. For instructions on how to set up TablePlus, refer to our wiki [here](https://www.notion.so/tryfractal/Using-TablePlus-to-Access-our-PostgresSQL-Database-d5badb38eb3841deb56a84698ccd20f5).

### Heroku Setup

For continuous integration and delivery, we leverage Heroku pipelines, which provides us with automated PR testing, isolation of environment variables, promotion/rollbacks, and auto-deploys from Github. Contributors should NOT push code to Heroku; only codeowners are expected to do this. Instead, contributors should PR their changes into the appropriate Github branch (most often `master`).

While our Heroku pipeline should not be modified without codeowner permission, it is helpful to understand how it works by consulting our wiki [here](https://www.notion.so/tryfractal/Heroku-CI-CD-Pipeline-Webservers-f8ef5b43edc84c969cf005fcac4641ba).

### GraphQL

We leverage Hasura GraphQL (hosted on Heroku) to enable real-time database access and serverless database retrieval. For pure SQL requests, we encourage using GraphQL instead of writing your own server endpoint to minimize the amount of code we write and because GraphQL has a lot of really nice built-in features.

GraphQL is already set up, but here's a [setup doc](https://hasura.io/docs/1.0/graphql/core/deployment/deployment-guides/heroku.html) for reference. Hasura GraphQL also provides a console for easy interfacing with the database. The prod database console is [here](prod-database.fractal.co) using access key stored as a config variable in the Heroku `fractal-graphql` application.

If you want to test with local GraphQL endpoints, running `bash hasura_run.sh` in the `docker` subdirectory will create a local instance of Hasura at `localhost:8080`. It currently uses the dev database with admin secret `secret` and auth hook `http://host.docker.internal:7730/hasura/auth` (assuming that you're running a local instance of the webserver through docker), but change as needed to get the ports you want.

## Testing

### Setting Up

You need the CLI utilities `pg_dump` and `psql`. On Mac:

```
brew install postgresql
```

First, we need to setup a local Redis and Postgres instance. Navigate to `tests/setup` and run `bash setup_tests.sh`. This only has to be run once for as long as you are testing. This script will use `docker-compose` to set up a local db that looks like a fresh version of the remote dev db. It'll be mostly empty except for a few tables. You can use TablePlus to connect to it locally at `localhost:9999`. You can find the username and database (pwd optional) in `docker/.env`. When you are done testing, end the containers with `docker-compose down`. Note: the `setup_tests.sh` script saves SQL scripts to `main-webserver/db-setup`. Delete these once in a while to get an updated pull of the database.

### Testing

Now, navigate to `tests` and run `bash run_tests.sh`. This loads the environment variables in `docker/.env` and uses `pytest` to run the tests. If something goes wrong during testing and you kill it early, clean up clusters using the AWS console. Note that since the db is local and ephemeral, any db changes can be safely done.

### Manual Testing

Sometimes, it is helpful to manual test changes that don't neatly fit into the current unit testing / integration testing framework. For example, let's say you want to make sure you properly wrote a database filtering command using the SQLAlchemy ORM. This section briefly describes different kinds of manual testing and how to do them.

_Manual Testing - Database_

To test database specific things, you can make a file `db_manual_test.py` in main-webserver root with the following contents:

```python
# export the URL of the database as DATABASE_URL
# if local eph db (launched with docker/local_deploy.sh):
# export DATABASE_URL=postgres://<USER>@localhost:9999/<DB>
# You can find USER and DB in docker/.env after retrieving the config
from app.factory import create_app
from app.models import UserContainer

app = create_app()

with app.app_context():
    base_container = (
        UserContainer.query.filter_by(
            is_assigned=False, task_definition="fractal-dev-browsers-chrome", location="us-east-1"
        )
        .filter(UserContainer.cluster.notlike("%test%"))
        .with_for_update()
        .limit(1)
        .first()
    )

    print(base_container)
```

If you need to populate some fake data into the ephemeral db, use TablePlus. Fun fact - this is exactly the code we use to get existing prewarmed containers. This kind of manual testing provides a neat way to quickly test and debug the database logic in isolation.

_Manual Testing - Deployments_

TODO: explain review apps here, link notion.

## How To Contribute

Before making a pull request, ensure that the following steps are taken:

1. Make sure that your code follows the guidelines outlined in our [Python Webserver Coding Philosophy](https://www.notion.so/tryfractal/Code-Philosophy-Webserver-backend-d036205444464f8b8a61dc36eeae7dbb).

2. Lint your code by running `black .` and `pylint app` from the `main-webserver` directory. If this does not pass, your code will fail Github CI. NOTE: Depending on where the directory containing your virtual environment is located, `black .` may attempt to lint the source code for all of the packages specified in your requirements files. In this case, use the --exclude flag.

3. Run all test files by running `bash run_tests.sh` in the `main-webserver/tests` directory. NOTE: If you have written new functions, make sure it has a corresponding test, or code reviewers will request changes on your PR.

4. Rebase against `dev` by pulling `dev` and running `git rebase dev`.

Finally, you can open a PR to `dev`.

## Styling

To ensure that code formatting is standardized, and to minimize clutter in the commits, you should set up styling with [Python Black](https://github.com/psf/black) before making any PRs. We have [pre-commit hooks](https://pre-commit.com/) with Python Black support installed on this project, which you can initialize by first installing pre-commit via `pip install pre-commit` and then running `pre-commit install` to instantiate the hooks for Python Black. We may also add Pylint/flake8 in future, which enables import error checking.

You can always run Python Black directly from a terminal by first installing it via `pip install black` (requires Python 3.8+) and running `black .` to format the whole project. You can see and modify the project's Python Black settings in `pyproject.toml` and list options by running `black --help`. If you prefer, you can also install it directly within your IDE by via the following instructions:

### [VSCode](https://medium.com/@marcobelo/setting-up-python-black-on-visual-studio-code-5318eba4cd00)

1. Install it on your virtual env or in your local Python with the command: `pip install black`

2. Now install the Python extension for VS-Code by opening your VSCode, typing “Ctrl + P”, and pasting the line below:

```
ext install ms-python.python
```

3. Go to the settings in your VSCode by typing “Ctrl + ,” or clicking at the gear on the bottom left and selecting “Settings [Ctrl+,]” option.

4. Type “format on save” at the search bar on top of the Settings tab and check the box.

5. Search for “Python formatting provider” and select “Black”.

6. Now open/create a Python file, write some code and save it to see the magic happen!

# FAQ

### How do the Flask and Celery applications know where Redis is running?

It depends.

For Heroku deployments (i.e. Heroku apps including review apps and CI apps), both applications try to read the value of the `REDIS_TLS_URL` environment variable, falling back on the value of the `REDIS_URL` environment variable if `REDIS_TLS_URL` is not set and failing if `REDIS_URL` is not set.

For local deployments, including local test deployments, both applications may try to generate a Redis connection string from the environment variables `REDIS_DB`, `REDIS_HOST`, `REDIS_PASSWORD`, `REDIS_PORT`, `REDIS_SCHEME`, and `REDIS_USER`. The connection string is constructed as follows:

```bash
REDIS_URL="$REDIS_SCHEME://$REDIS_USER:$REDIS_PASSWORD@$REDIS_HOST:$REDIS_PORT/$REDIS_DB"
```

If not set, the environment variables assume the following default values:

- `REDIS_SCHEME`: `rediss://`
- `REDIS_USER`: the empty string
- `REDIS_PASSWORD`: the empty string
- `REDIS_HOST`: `localhost`
- `REDIS_PORT`: `6379`
- `REDIS_DB`: `0`

**However,** the presence of the environment variable `REDIS_URL` will prevent a connection string from being generated from these six environment variables. Instead, the value of the `REDIS_URL` environment variable will be read.

In case the Redis connection string identifies an invalid Redis instance (e.g. the connection string is `rediss://`, but there is either no Redis instance running on `localhost:6379` or it is running, but without TLS), any attempts to connect to the instance identified by the connection string will immediately cause an exception.

In most cases, you should not have to worry about setting any of these environment variables manually, as `REDIS_URL` will be set automatically in `docker-compose.yml` files.

### What is the general structure of our codebase and the purpose of specific files?

A tree structure is provided below:

Note that all conftest files contain test fixtures for their respective directories, and all other files in the directory not shown are empty/autogenerated.

```
.
├── Procfile --> the file that tells Heroku what workers we need
(web and celery)
├── analytics --> a directory for webserver-related load testing/analytics
│   └── profiler
│       ├── profiler.py --> load testing code
│       └── tasks.py --> mock tasks for load testing
├── app
│   ├── __init__.py --> initialization code/global preprocessors for the app
│   ├── blueprints -->  our API endpoints are described here
│   │   ├── admin --> API endpoints relating to manipulating user and logs info
│   │   │   ├── admin_blueprint.py --> endpoint for logging in as admin
│   │   │   ├── hasura_blueprint.py --> endpoint we use to authenticate GQL queries
│   │   │   └── logs_blueprint.py --> endpoint we use to upload logs
│   │   ├── auth
│   │   │   ├── account_blueprint.py --> endpoints we use to log into accounts/verify accounts
│   │   │   ├── google_auth_blueprint.py --> endpoints we use to log in via google
│   │   │   └── token_blueprint.py --> endpoints we use to generate JWT tokens
│   │   ├── aws
│   │   │   └── aws_container_blueprint.py --> endpoints we use to create, delete, and manipulate AWS resources
│   │   ├── celery
│   │   │   └── celery_status_blueprint.py -->  endpoint to check the status and results of celery tasks
│   │   ├── host_service
│   │   │   └── host_service_blueprint.py --> endpoints that handle host service handshakes
│   │   ├── mail
│   │   │   ├── mail_blueprint.py --> endpoints for generating and sending emails
│   │   │   └── newsletter_blueprint.py --> endpoints for sending out emails to a mailing list
│   │   ├── oauth.py --> endpoints for OAuth/ storage connection
│   │   └── payment
│   │       └── stripe_blueprint.py --> endpoints for handling stripe transactions/subscriptions
│   ├── celery
│   │   ├── aws_celery_exceptions.py --> Exceptions that are unique to AWS tasks
│   │   ├── aws_ecs_creation.py --> Celery tasks related to creating AWS containers and clusters
│   │   ├── aws_ecs_deletion.py --> Celery tasks related to deleting AWS containers and clusters
│   │   ├── aws_ecs_modification.py --> Celery tasks related to AMI updates and scaling
│   │   ├── aws_s3_deletion.py --> Celery tasks related to deleting S3 logs
│   │   ├── aws_s3_modification.py --> Celery tasks related to modifying and uploading S3 logs
│   │   └── dummy.py --> Sample celery task, used for testing
│   ├── celery_utils.py --> Celery config and setup scripts
│   ├── config.py --> General app config and setup utils
│   ├── constants --> Constants used throughout our program
│   │   ├── bad_words_hashed.py --> hashes of inappropriate usernames
│   │   ├── container_state_values.py --> all possible AWS container states
│   │   ├── generate_subsequences_for_words.py --> substring testing to find inappropriate usernames
│   │   ├── hash_bad_words.py --> what we use to generate bad_words_hashed
│   │   ├── http_codes.py -->  HTTP response codes we return in our app
│   │   ├── states.py --> list of state abbreviations (e.g. CT for Connecticut)
│   │   └── time.py --> useful constants about time (like 60 seconds per minute)
│   ├── exceptions.py --> exceptions we use throughout the app
│   ├── factory.py -->  general application setup scripts
│   ├── helpers --> helper utils we use throughout the codebase
│   │   ├── blueprint_helpers --> any complex synchronous computation that's part of our endpoints
│   │   │   ├── admin
│   │   │   │   ├── admin_post.py --> helpers for admin login
│   │   │   │   └── hasura_get.py --> helpers for GQL token get requests
│   │   │   ├── auth
│   │   │   │   ├── account_get.py --> helpers related to account get requests (e.g. user info retrieval)
│   │   │   │   ├── account_post.py --> helpers related to account post reqs (e.g. login)
│   │   │   │   ├── google_auth_post.py --> helpers related to google login
│   │   │   │   └── token_post.py --> helpers related to JWT token generation/update
│   │   │   ├── aws
│   │   │   │   ├── aws_container_post.py --> helpers for AWS post reqs that _don't_ require celery tasks -- e.g protocol info
│   │   │   │   └── container_state.py --> helpers that retrieve the current state of user containers (for client-app)
│   │   │   ├── mail
│   │   │   │   └── mail_post.py --> helpers that generate emails to send to users
│   │   │   └── payment
│   │   │       └── stripe_post.py --> helpers that execute stripe operations
│   │   ├── helpers_tests
│   │   │   ├── aws_tests
│   │   │   │   └── test_ecs_client.py --> basic tests of ECSClient functionality
│   │   └── utils
│   │       ├── aws -- > utility scripts for interfacing with AWS
│   │       │   ├── autoscaling.py --> scripts for manipulating ASGs and their associated clusters
│   │       │   ├── aws_general.py --> a few general utilities for AWS
│   │       │   ├── aws_resource_integrity.py --> scripts that ensure certain AWS resources exist
│   │       │   ├── aws_resource_locks.py --> scripts to ensure atomicity on AWS resource use
│   │       │   ├── base_ecs_client.py -->  The majority of our AWS code lives here -- interfaces to clusters, containers, ASGs,
                and assorted other AWS endpoints
│   │       │   ├── base_userdata_template.sh --> a template for the code executed on new EC2 instances at startup
│   │       │   ├── ecs_deletion.py --> code used for cluster deletion
│   │       │   └── utils.py --> general utility scripts for API reqs -- mostly retry code
│   │       ├── datadog
│   │       │   ├── event_tags.py --> code for generating datadog event tags
│   │       │   ├── event_text.py --> code for generating datadog event content
│   │       │   └── events.py --> code for storing datadog events/generating them
│   │       ├── general
│   │       │   ├── auth.py  --> decorators for authenticating users/devs
│   │       │   ├── crypto.py --> code for password encryption/hashing
│   │       │   ├── limiter.py --> our rate limiter config
│   │       │   ├── logs.py --> our webserver logging config
│   │       │   ├── sql_commands.py --> helpers for SQL commit, update, and deletion
│   │       │   ├── time.py --> useful scripts for working with time/date
│   │       │   └── tokens.py --> utils for generating JWTs
│   │       ├── locations
│   │       │   └── location_helper.py --> script to find client app location from IP address
│   │       ├── mail
│   │       │   └── mail_client.py --> helpers for mail generation
│   │       └── payment
│   │           └── stripe_client.py --> helpers for stripe
│   ├── maintenance
│   │   └── maintenance_manager.py --> helpers for webserver maintenance mode
│   ├── models --> Python classes corresponding to our DB tables
│   │   ├── _meta.py -->  the scripts initializing SQLAlchemy
│   │   ├── hardware.py -->  tables in our hardware schema
│   │   ├── logs.py -->  tables in our logs schema
│   │   ├── oauth.py -->  tables in our oauth schema
│   │   ├── public.py -->  tables in our public schema
│   │   └── sales.py -->  tables in our sales schema
│   └── serializers --> object-to-json conversion for our ORM.  Structure mirrors models
│       ├── hardware.py
│       ├── logs.py
│       ├── oauth.py
│       ├── public.py
│       └── sales.py
├── app.json -->  structure of our app/heroku config
├── bin -->  scripts needed for heroku app deployment
│   ├── compile
│   ├── detect
│   └── util.sh
├── db_migration --> code that governs DB migration
│   ├── config.py  --> config values for the migration
│   ├── errors.py --> exception handling for the migration
│   ├── migration_containers.py --> scripts handling docker for migrations
│   ├── postgres.py --> scripts initializing postgres for migrations
│   ├── schema.sql --> the current schema of our db
│   ├── schema_diff.py --> a diff between the current schema on branch and the one we're merging into
│   ├── schema_dump.py --> a schema autodumper script
│   └── utilities.py --> assorted utils for the above
├── db_setup --> scripts to make an ephemeral DB
│   ├── db_setup.sh --> script that preps ephemeral DB
│   ├── fetch_db.sh --> script that fetches the current dev DB
│   └── modify_ci_db_schema.py --> scripts that eliminate extraneous info (db users) from the schema
├── docker --> scripts for running a local config of the webserver
│   ├── Dockerfile --> docker container for local webserver
│   ├── docker-compose.yml --> container instructions for local webserver
│   ├── hasura_run.sh --> script to enable hasura console on local server
│   ├── local_deploy.sh --> script to run to turn on local server
│   ├── pgparse.py --> util for pulling down config info for local server
│   └── retrieve_config.sh --> script to pull down config info for local server
├── entry.py --> Heroku app entrypoint, starts up web workers
├── pylintrc --> pylint config
├── pyproject.toml --> Black config
├── pytest.ini --> pytest config
├── requirements-test.txt --> test package requirements
├── requirements.txt --> packages you need to run the webserver
├── runtime.txt --> desired python version
├── scripts
│   └── poll_maintenance.sh --> script to check on webserver maintenance status
├── stem-cell.sh --> the generator of our web and celery workers
└── tests --> tests for our assorted endpoints.  All files without docs
    should be assumed to unit test the endpoints/bps they name.
    ├── admin
    │   └── test_logs.py
    ├── aws
    │   ├── config.py
    │   ├── test_assign.py
    │   ├── test_aws_container.py --> tests for end-to-end AWS task generation functionality
    │   ├── test_cluster.py --> tests for cluster deletion
    │   ├── test_delete.py
    │   ├── test_info.py
    │   ├── test_ping.py
    │   ├── test_serialize.py --> tests for serialization of container objects from DB
    │   └── test_stun.py
    ├── constants --> useful constants for testing
    │   ├── api_keys.py --> demo API for mailslurp mail testing
    │   └── settings.py --> pytest settings
    ├── hasura
    │   └── test_hasura_auth.py
    ├── helpers
    │   └── general
    │       └── progress.py
    ├── mail
    │   └── test_mail.py
    ├── maintenance
    │   └── test_webserver_maintenance.py --> integration tests for webserver maintenance mode
    ├── misc
    │   └── test_rate_limiter.py
    ├── oauth
    │   ├── test_cloud_storage.py
    │   ├── test_credential.py
    │   └── test_oauth.py
    ├── patches.py --> useful monkeypatches for all our tests
    ├── payment
    │   └── test_stripe_client.py
    ├── run_tests.sh --> shell script that runs our tests with all desired setup/settings/teardown
    ├── setup
    │   ├── docker-compose.yml --> stands up all docker containers you need for local testing
    │   └── setup_tests.sh --> script that prepares local db/services for testing
    └── test_misc.py --> tests some of our utility scripts
```
