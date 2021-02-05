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

The web application stack is comprised of three main components: the web server itself, an asynchronous task queue, and a database. The web server is written in Python using the [Flask](https://flask.palletsprojects.com/en/1.1.x/) web framework. The task queue is a Redis-backed [Celery](https://docs.celeryproject.org/en/stable/index.html) task queue. As such, the task queue can be broken down into two more granular sub-components: a pool of worker processes and a Redis store. The database is a Postgres instance that is shared by multiple developers. In summary, there are a total of _four_ components that make up the web application stack: a Flask server, a Celery worker pool, a Redis store, and a PostgreSQL database.

We use [`docker-compose`](https://docs.docker.com/compose/) to spin part of the web server stack up (the `docker-compose` stack does not include the Postgres database, which is shared between multiple developers and app deployments, as mentioned above) locally for development purposes. `docker-compose` builds Docker images for the Flask server and the Celery worker pool and deploys them alongside containerized Redis. There is also a `pytest` test suite that developers may run locally.

We use environment variables to configure our local development environments. Environment variables should be set by adding lines of the form `KEY=VALUE` to the file `docker/.env`. **The main environment variable that _must_ be set in order to do any kind of local development, whether with the `docker-compose` stack or the `pytest` test suite, is the `CONFIG_DB_URL` environment variable.** `CONFIG_DB_URL` specifies the PostgreSQL connection URI of the Fractal configuration database. This database contains default values for many of the variables that are used to configure the various parts of the web application stack. These default values may be overridden locally by setting alternatives in the same `docker/.env` file.

The following environment variables must also be set in `docker/.env` (neither the test suite nor the `docker-compose` stack will work without them).

- `POSTGRES_DB` &ndash; The name of the Postgres database to which to connect.
- `POSTGRES_HOST` &ndash; The hostname or IP address of the development Postgres instance.
- `POSTGRES_PASSWORD` &ndash; The password used to authenticate with the local stack's PostgresQL instance.
- `POSTGRES_USER` &ndash; The name of the user as whom to log into the development Postgres instance.

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

    aws s3 cp s3://fractal-dev-secrets/client_secret.json client_secret.json

from within the `main-webserver` directory.

This step is optional. If you choose not to complete this step, you will still be able to launch containers, but those containers will not have cloud storage access, even for accounts to which a cloud storage provider is connected.

**4. Make Dummy SSL Certificates.**

We use a Redis+TLS instance in production, so during testing we try to maintain similarity by using a Redis+TLS docker image. Navigate to the `dummy_certs` directory and run `bash create_dummy_certs.sh` to generate temporary useless SSL certificates. The Redis+TLS docker image will use this.

**5. Spin Up Local Servers**

Use `docker-compose` to run the stack locally. First, `cd` into the `docker/` folder. Then, run the `up` command. If you are on Windows, you should run this from a command prompt in Administrator mode. This will start the app at `run.py`.

```sh
docker-compose up --build
```

If you encounter a "daemon not running" error, this likely means that Docker is not actually running. To fix this, try restarting your computer or opening the Docker desktop app; if the app opens successfully, then the issue should go away.

Review `docker-compose.yml` to see which ports the various services are hosted on. For example, `"7810:6379"` means that the Redis service, running on port 6379 internally, will be available on `localhost:7810` from the host machine. Line 25 of `docker-compose.yml` will tell you where the webserver itself is running.

By default, hot-reloading of the Flask web server and Celery task queue is disabled (`HOT_RELOAD=`). To enable it, set `HOT_RELOAD` to a non-empty string in your `docker/.env` file.

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

## Testing

### Setting Up

You need the CLI utilities `pg_dump` and `psql`. On Mac:

```
brew install postgresql
```

First, we need to setup a local Redis and Postgres instance. Navigate to `tests/setup` and run `bash setup_tests.sh`. This only has to be run once for as long as you are testing. This script will use `docker-compose` to set up a local db that looks like a fresh version of the remote dev db. It'll be mostly empty except for a few tables. You can use TablePlus to connect to it locally at `localhost:9999`. You can find the username and database (pwd optional) in `docker/.env`. When you are done testing, end the containers with `docker-compose down`. Note: the `setup_tests..sh` script saves SQL scripts to `tests/setup/db`. Delete these once in a while to get an updated pull of the database.

### Testing

Now, navigate to `tests` and run `bash run_tests.sh`. This loads the environment variables in `docker/.env` and uses `pytest` to run the tests. If something goes wrong during testing and you kill it early, clean up clusters using the AWS console. Note that since the db is local and ephemeral, any db changes can be safely done.

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
