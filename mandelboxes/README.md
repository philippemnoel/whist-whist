# Whist Mandelbox Images

At Whist, we use the term "mandelbox" to refer to what is essentially an application that we deliver, combined with the Whist-specific customizations that make everything work (securely). At the moment, our mandelboxes run on Docker.

This repository contains the code for "mandelbox-izing" the various applications that Whist streams. The base Dockerfile running the Whist protocol is under the `/base/` subfolder, and is used as a starter image for the application Dockerfiles which are in each of their respective application-type subfolders. This base image runs **Ubuntu 20.04** and installs everything needed to interface with the drivers and the Whist protocol.

## Development

To contribute to enhancing all the mandelbox images Whist uses, you should contribute to the base Dockerfile under `/base/`, unless your changes are application-specific, in which case you should contribute to the relevant Dockerfile for the application in question. We strive to make mandelbox images as lean as possible to optimize for concurrency and reduce the realm of security attacks possible.

### Getting Started

After cloning the repo, set up your EC2 instance with the setup script from the `host-setup` subrepo.:

```bash
./setup_localdev_dependencies.sh
```

This will begin installing all dependencies and configurations required to run our mandelbox images on an AWS EC2 host. After the setup scripts run, you must `sudo reboot` for Docker to work properly. After rebooting, you may finally build the protocol and the base image by running:

```bash
../protocol/build_protocol_targets.sh WhistServer && ./build_mandelbox_image.sh base && ./run_local_mandelbox_image.sh base
```

⚠️ If the command above errors due a failure to build the base container image, try running `docker system prune -af` first (see paragraph below for more context on the issue).

### Building Images

To build the server protocol for use in a mandelbox image (for example with the `--update-protocol` parameter to `run_mandelbox_image.sh`), run:

```bash
../protocol/build_protocol_targets.sh WhistServer
```

To build a specific application's mandelbox image, run:

```bash
./build.sh APP
```

This takes a single argument, `APP`, which is the path to the target folder whose application mandelbox you wish to build. For example, the base mandelbox is built with `./build_mandelbox_image.sh base` and the Chrome mandelbox is built with `./build_mandelbox_image.sh browsers/chrome`, since the relevant Dockerfile is `browsers/chrome/Dockerfile.20`. This script names the built image as `whist/$APP`, with a tag of `current-build`.

You first need to build the protocol and then build the base image before you can finally build a specific application image.

⚠️ The `./build_mandelbox_image.sh APP` command can sometimes fail due to `apt` being unable to fetch some archives. In that case, if you re-run with the `-o` flag, you should be able to see an error that looks like this: `E: Unable to fetch some archives, maybe run apt-get update or try with --fix-missing?`). To fix the problem, call `docker system prune -af` first, then run `./build_mandelbox_image.sh APP` again.

**NOTE:** For production we do not cache builds of mandelboxes. This is for two reasons:

1. Using build caches will almost surely lead to outdated versions of packages being present in the final images, which exposes publicly-known security flaws.
2. It is also more expensive to keep a builder machine alive 24/7 than to just build them on the fly.

### Running Local Images

Before you can run mandelbox images (local or remote), make sure you have the host service running in a separate terminal with `cd ../host-service && make run`.

Once an image with tag `current-build` has been built locally via `build_mandelbox_images.sh`, it may be run locally by calling:

```bash
./run.sh APP [OPTIONS...]
```

As usual, `APP` is the path to the app folder. Note that this script should be used on EC2 instances as an Nvidia GPU is required for our mandelboxes and our protocol to function properly.

There are some other options available to control properties of the resulting mandelbox, like whether the server protocol should be replaced with the locally-built version. Run `./run_local_mandelbox_image.sh --help` to see all the other configuration options.

### Connecting to Images

If you want to save your configs between sessions, then pass in a user ID and config encryption token. In case you don't want the server protocol to auto-shutdown after 60 seconds, you can set the timeout with another argument. As mentioned above, pass in `--help` to one of the mandelbox image-running scripts to see all the available options.

## Publishing

We store our production mandelbox images on GitHub mandelbox Registry (GHCR) and deploy them on EC2 via our host service.

### Manual Publishing

Once an image has been built via `./build.sh APP` and therefore tagged with `current-build`, that image may be manually pushed to GHCR by running (note, however, this is usually done by the CI. You shouldn't have to do this except in very rare circumstances. If you do, make sure to commit all of your changes before building and pushing):

```bash
GH_PAT=xxx GH_USERNAME=xxx ./push.sh APP ENVIRONMENT
```

Replace the environment variables `GH_PAT` and `GH_USERNAME` with your GitHub personal access token and username, respectively. Here, `APP` is again the path to the relevant app folder; e.g., `base` or `browsers/chrome`. Environment is either `dev`, `staging`, `prod`, or nothing. The image is tagged with the full git commit hash of the current branch.

### Continuous Delivery

For every push to `dev`, `staging`, or `prod`, all applications that have a Dockerfile get automatically built and pushed to GHCR. These images are then loaded up into AMIs and deployed.

### Useful Debugging Practices

If `./build.sh` is failing, try running with `./build-mandelbox_images.sh -q` to redirect logging output to files. Then, go to the failing image and check `build.log`. For example, you might look in `browsers/chrome/build.log`.

If the error messages seem to be related to fetching archives, try `docker system prune -af`. It's possible that Docker has cached out-of-date steps generated from an old mandelbox image, and needs to be cleaned and rebuilt.

## Styling

We use [Hadolint](https://github.com/hadolint/hadolint) to format the Dockerfiles in this project. Your first need to install Hadolint via your local package manager, i.e. `brew install hadolint`, and have the Docker daemon running before linting a specific file by running `hadolint <file-path>`.

We also have [pre-commit hooks](https://pre-commit.com/) with Hadolint support installed on this project, which you can initialize by first installing pre-commit via `pip3 install pre-commit` and then running `pre-commit install` in the project folder, to instantiate the hooks for Hadolint. Dockerfile improvements will be printed to the terminal for all Dockerfiles specified under `args` in `.pre-commit-config.yaml`. If you need/want to add other Dockerfiles, you need to specify them there. If you have issues with Hadolint tracking non-Docker files, you can commit with `git commit -m [MESSAGE] --no-verify` to skip the pre-commit hook for files you don't want to track.

## FAQ

### I have the correct IP and ports for a mandelbox on my EC2 dev instance to connect to, but it looks like my protocol server and client aren't seeing each other!

Make sure your dev instance has a security group that has at least the ports [1025, 49151) open for incoming TCP and UDP connections.

### My Nvidia drivers stopped working!

To fix this, you can either (a) disable rolling kernel updates or (b) hardcode the kernel version in `/etc/default/grub` and refresh GRUB. We suggest you do B.
