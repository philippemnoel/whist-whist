# Whist Base Mandelbox Image

This subfolder contains the base mandelbox Dockerfile that builds the core Whist mandelbox running Whist and general utilities and services that application-specific Dockerfiles can build upon. The base image runs **Ubuntu 20.04**.

This subfolders conforms to the **Styling** and **Continous Integration** practices defined in the outer README.

## Development

This repository calls the Whist [Protocol](https://github.com/fractal/protocol) repository as a Git submodule for integrating our streaming technology in the Dockerfile. Refer to the outer README for instructions on fetching the protocol Git submodule and building it into the Dockerfile.

Any Linux services needed for the mandelboxes to function properly should be developed and hosted as part of this project subfolder. For any details on continuous integration and styling, refer to the outer README of this project.

### Architecture

The base mandelbox, specified by Dockerfile.20, is oriented around **systemd**, which is the entrypoint to the mandelbox. To specify what code actually runs in the mandelbox after startup, we copy over a set of systemd services at buildtime that, when systemd runs, will do some runtime configuration and start all the processes that we need.

### Services

The services `startup/fractal-startup.service`, `display/fractal-display.service`, `audio/fractal-audio.service` and `main/fractal-main.service` must run successfully for the mandelbox to be functional. These services run in the order written above and are run by systemd on mandelbox start.

1. `startup/entrypoint.sh` starts up and calls `exec` to become systemd (with pid 1)

2. systemd runs `startup/fractal-startup.service`, which waits until a `.ready` file is written -- this is configured as a oneshot, meaning that `display/fractal-display.service` only starts after `startup/fractal-startup.service` finishes. This means that everything waits for `.ready` file to be written. The directory with the `.ready` file also contains files with the parameters necessary to for the mandelbox to start properly (user ID, etc.).

3. Once `startup/fractal-startup.service` finishes, `display/fractal-display.service` starts an X Server with the proper configuration that we need. Note that this starts an X Server that is powered by an Nvidia GPU, meaning our mandelboxes can only be run on GPU-powered hosts.

4. `audio/fractal-audio.service`, meanwhile, can start as soon as `display/fractal-display.service` _begins_ running (which is important because the lifecycle of fractal-display is the lifecycle of our mandelboxized applications). It starts a virtual Pulse Audio soundcard in the mandelbox, enabling sound.

5. `main/fractal-main.service` can start as soon as `display/fractal-display.service` and `audio/fractal-audio.service` are both running, running the Whist protocol and configuring some environment variables to work correctly with the X Server.

### Useful Debugging Practices

You can run `systemctl status` to see all the running services in the mandelbox. To be more precise, if you run it inside the mandelbox, you'll see all the services running in the mandelbox and the subprocesses they've started. If you run it outside the mandelbox on a Linux host machine, you'll see all the host services mixed in together with the mandelbox's services.

`journalctl -e` shows the end of the systemd logs (you can page further backwards by hitting `b`).

`journalctl` shows all the systemd logs. If you run `journalctl` in a Bash shell inside a mandelbox, you'll be able to see the logs of a running server protocol, which is useful for debugging.

`/usr/share/fractal/server.log` contains the Whist protocol logs. They can also be found in the systemd logs with `journalctl | grep FractalServer`.

`/testing_scripts` contains some little test scripts that can help debug problems on the host machine.

## Running

See **Running Local Images** and **Running Remote-Pushed Images** in the outer README.
