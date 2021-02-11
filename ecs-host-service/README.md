# Fractal ECS Host Service

This subfolder contains the code for the Fractal ECS host service, which is responsible for orchestrating containers on Fractal EC2 instances, which are referred to as **hosts** throughout the codebase. The host service is responsible for making Docker calls to start and stop containers, for enabling multiple containers to run concurrently on the same host by dynamically assigning TTYs, and for passing startup data to the containers from the Fractal webserver(s), like DPI and third-party cloud storage providers folders to mount.

## Development

### Building

In order for `go` to successfully download the private repositories that are dependencies of the host service, you'll need to configure your `git` to use `ssh` instead of `https`, like so:
```shell
git config --global url.ssh://git@github.com/.insteadOf https://github.com/
```

To build the service, install Go via your local package manager, i.e. `brew install go` on macOS, `sudo snap install --classic --channel=1.15/stable go` on Linux, or `choco install golang` on Windows, and then run `make`. Note that the host service is only meant to build on Go versions >= 1.15. Also, be sure to add `~/go/bin` to your `$PATH` variable as follows:
```shell
PATH=$PATH:~/go/bin
```

This will build the service under directory `/build` as `ecs-host-service`.

### Running

It is only possible to run the host service on AWS EC2 instances, since the host service code retrieves metadata about the instance on which it is running from the EC2 instance metadata endpoint <http://169.254.169.254/latest/meta-data/>. According to the [EC2 documentation](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/instancedata-data-retrieval.html), "the IP address `196.254.169.254` is a link-local address and is valid only from the [EC2] instance."

From an EC2 instance, you can run the host service via `make run`. Note that the service must be run as `root` since it manages `systemd` and `docker`, so make sure that your Linux user has permission to use `sudo` and be prepared to supply your password.

If you want to test the host service with our production Sentry configuration, use the command `make runprod`. Note that this will count against our Sentry logging quotas, and also attempt to start the ECS Agent! As such, we only recommend you try to do that on an Fractal-optimized AWS EC2 instance (see `ecs-host-setup/`).

### Design Decisions

This service will not restart on crash/panic, since that could lead to an inconsistency between the actually running containers and the data left on the filesystem. Instead, we note that if the service crashes, no new containers will be able to report themselves to the Fractal webserver(s), meaning there will be no new connections to the EC2 host and once all running containers are disconnected, the instance will be spun down.

We never use `os.exit()` or any of the `log.fatal()` variants, since we want to send out a message to our Fractal webserver(s) and/or Sentry upon the death of this service (this is done with a `defer` function call, which runs after `panic` but not after `exit`).

For more details, see the comments at the beginning of `main()` and `shutdownHostService()`.

## Styling

We use `goimports` and `golint` for Golang linting and coding standards. You can install them by running:

```
# Install goimports and golint
go get -u golang.org/x/tools/cmd/goimports
go get -u golang.org/x/lint/golint

# Add Go bin to path (Unix)
PATH=$PATH:~/go/bin
```

Then, format your code with `goimports [path-to-file-to-lint.go]` and `golint [path-to-file-to-format.go]`, respectively.

## Publishing

The Fractal host service gets automatically published to an AWS S3 bucket, `fractal-ecs-host-service`, through the `fractal-publish-build.yml` GitHub Actions workflow, under the `ecs-host-service-publish-build-s3` job. The `userdata-bootstrap.sh` script, which gets executed at runtime on the AWS EC2 hosts by the Fractal webserver(s), pulls the latest version of the Fractal host service from its AWS S3 bucket onto the deployed EC2 instance deployed. Once automated AMI-building is complete, we plan on integrating the Fractal ECS host service directly by updating the AMIs, without needing to upload the Fractal host service to AWS S3.
