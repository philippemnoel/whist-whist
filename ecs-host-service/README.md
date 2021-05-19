# Fractal ECS Host Service

This subfolder contains the code for the Fractal ECS host service, which is responsible for orchestrating containers on Fractal EC2 instances, which are referred to as **hosts** throughout the codebase. The host service is responsible for making Docker calls to start and stop containers, for enabling multiple containers to run concurrently on the same host by dynamically assigning TTYs, and for passing startup data to the containers from the Fractal webserver(s), like DPI. If you are just interested in what endpoints the host service exposes (i.e. for developing on the client application or webserver, check the file `httpserver/server.go`).

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

If you want to test the host service with our production Sentry configuration, use the command `make runprod`. Note that this will count against our Sentry logging quotas, and also attempt to start the ECS Agent! As such, we only recommend you try to do that on an Fractal-optimized AWS ECS instance that was started by the webserver (see `ecs-host-setup/`).

### Design Decisions

This service will not restart on crash/panic, since that could lead to an inconsistency between the actually running containers and the data left on the filesystem. Instead, we note that if the service crashes, no new containers will be able to report themselves to the Fractal webserver(s), meaning there will be no new connections to the EC2 host and once all running containers are disconnected, the instance will be spun down.

We never use `os.exit()` or any of the `log.fatal()` variants, or even plain `panic()`s, since we want to send out a message to our Fractal webserver(s) and/or Sentry upon the death of this service (this is done with a `defer` function call, which runs after `logger.Panic()` but not after `exit`).

For more details, see the comments and deferred cleanup function at the beginning of `main()`.

We use [contexts](https://golang.org/pkg/context/) from the Go standard library extensively to scope requests and set timeouts. [Brushing up on contexts](https://blog.golang.org/context) might prove useful.

For a list of changes made to the ECS agent codebase, see [the README in `ecsagent/agent`](./ecsagent/agent/README.md).

## Styling

We use `goimports` and `golint` for Golang linting and coding standards. These are automatically installed when running `make format` or `make lint`.

The easiest way to check that your code is ready for review (i.e. is linted, vetted, and formatted) is just to run `make check`. This is what we use to check the code in CI as well.

## Publishing

The Fractal host service gets built into our AMIs during deployment.

For testing, you can also use the `upload` target in the makefile, which builds a host service and pushes it to the `fractal-ecs-host-service` s3 bucket with value equal to the branch name that `make upload` was run from.

## Tree of host service files

```tree
.
├── ecsagent
│   ├── agent <- package containing our modified ecsagent code. For the fractal-specific changelog, see the README in this directory.
│   ├── ecsagent.go <- Our wrapper for the ecsagent code
│   └── seelog_bridge.go <- A wrapper for the ecsagent logging to integrate with our own
├── ecs-host-service.go <- The main file, contains the main logic and the most comments to explain the design decisions of the host service
├── fractalcontainer <- package for the abstraction of a container managed by Fractal
│   ├── fractal_container.go <- the main file in the fractalcontainer package, defines the types and implements the simple functions associated with Fractal-managed containers
│   ├── portbindings <- package for the abstraction of port bindings between the container and the host
│   │   ├── port_bindings.go <- provides helper functions for port bindings
│   │   └── transport_protocol.go <- provides the abstraction of a transport_protocol (used by the ecsagent as well)
│   ├── tracker.go <- keeps a list of all Fractal-managed containers at any given point
│   ├── ttys <- package to abstract away TTYs
│   │   └── ttys.go <- implementation of the ttys package
│   ├── uinputdevices <- package to abstract away uinput devices
│   │   ├── uinput <- package that provides actual uinput functionality that is consumed by the uinputdevices package. This package used to be a separate codebase, but got rolled into this one. See the README in this directory for more details.
│   │   └── uinput_devices.go <- Our wrapper for the uinput package, and implementation of the uinputdevices package
│   ├── user_configs.go <- Provides functions that manage user configs, including fetching, uploading, and encrypting
│   └── write_data_for_protocol.go <- Provides functions that communicate with the container itself by writing data (e.g. providing TTY mappings, etc.)
├── fractallogger <- Logging package
│   ├── constants.go <- Provides some constants that need to be available in every package, could eventually be rolled into its own package but it's only two lines long at the time of writing this.
│   ├── fractallogger.go <- Main logging file
│   ├── heartbeats.go <- Implementation of heartbeats to the webserver
│   ├── logzio_specifics.go <- Integration with logzio
│   ├── metadata.go <- Implementation of computing information about the host, like AWS-specific metadata or memory usage
│   └── sentry-specifics.go <- Integration with Sentry
├── httpserver <- Package that exposes HTTPS endpoints of the host service
│   └── server.go <- Infrastructure around those endpoints
├── Makefile
```
