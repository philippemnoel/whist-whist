# Fractal ECS Host Service

This repository contains the Fractal ECS host service, which is installed on AWS EC2 instances running Fractal containers via AWS ECS to perform dynamic tty allocation and ensure new containers can be safely allocated on the host machine as simultaneous requests come in.

## Development

### Building

To build the service, first install Go via your local package manager, i.e. `brew install go` (MacOS) or `sudo snap install --classic --channel=1.15/stable go` (Linux), and then run `make`. Note that this was only tested using Go version >= 1.15.

This will build the service under directory `/build` as `ecs-host-service`.

### Running

It is only possible to run the host service on AWS EC2 instances. The host service code retrieves metadata about the instance on which it is running from the EC2 instance metadata endpoint <http://169.254.169.254/latest/meta-data/>. According to [the EC2 documentation](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/instancedata-data-retrieval.html), "the IP address `196.254.169.254` is a link-local address and is valid only from the [EC2] instance."

Once you have access to an EC2 instance, you can run the host service with the command `make run`. Note that the service must be run as root because it has to do things like manage `systemd` and `docker`. Make sure your user has permission to use `sudo` and be prepared to supply your password after executing `make run`.

If you want to test the service with our production Sentry configuration, use the command `make runprod`. Note that this will count against our Sentry logging quotas, and also attempt to start the ECS Agent! As such, we only recommend you try to do that on an ECS-optimized AWS EC2 instance.

### Design Decisions

This service will not restart on crash/panic, since that could lead to an inconsistency between the actually running containers and the data left on the filesystem. Instead, we note that if the service crashes no new containers will be able to report themselves to the webserver, so there will be no new connections to the host, and once all running containers are disconnected, the instance will be spun down.

We never use `os.exit()` or any of the `log.fatal()` variants, since we want to send out a message to our webserver and/or Sentry upon the death of this service (this is done with a `defer` function call, which runs after `panic` but not after `exit`).

For more details, see the comments at the beginning of `main()` and `shutdownHostService()`.

## Styling

We use `goimports` and `golint` for proper linting and coding practices in this project. We use `goimports` to actually format our Go code, and we use `golint` to enforce proper Go coding practices. We recommend you use both in the pre-commit hooks. You can install them by running `pre-commit install`, after having installed the `pre-commit` package via `pip install pre-commit`.

You can install `goimports` and `golint` by running `go get -u golang.org/x/tools/cmd/goimports` and `go get -u golang.org/x/lint/golint`, respectively. You then need to add the Go commands to your path by running `PATH=$PATH:~/go/bin`, on Unix. You can then easily run the commands via `goimports path-to-file-to-lint.go` and `golint path-to-file-to-lint.go`.

## Publishing

This service gets automatically published with every push to `master` by a GitHub Actions workflow which uploads the executable to an S3 bucket, `fractal-ecs-host-service`, from which a script in the User Data section of the Fractal AMIs pulls the service into the EC2 instances deployed. Pushing to `master` will also trigger an automated Sentry release through GitHub Actions, and our following Sentry logs will be tagged with this release. For more details, see `.github/workflows/publish-build.yml` and `.github/workflows/sentry-release.yml`.
