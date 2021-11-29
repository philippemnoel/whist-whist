# Whist Host Service and Mandelboxes

This subfolder contains code for Whist's host service to start, configure, and shutdown mandelboxes.

## Passing information from host service to mandelboxes

There are a few ways information is passed from host service to mandelboxes. Information can be passed via a file, environment variable, or both.

### Writing resource mapping to file

First way is to use the `writeResourceMappingToFile` function defined in [mandelbox_params.go](https://github.com/fractal/fractal/blob/dev/host-service/mandelbox/mandelbox_params.go#L73). A user could explore the filesystem of the mandelbox to see those files, so it is very important that the filesystem does not contain any sensitive information or proprietary information.

The file with data will be written in the `WHIST_MAPPINGS_DIR` directory and can be accessed by the mandelbox. Scripts where the mandelbox accesses these files include [fractal-startup.sh](https://github.com/fractal/fractal/blob/dev/mandelboxes/base/startup/fractal-startup.sh) and [run-fractal-server.sh](https://github.com/fractal/fractal/blob/dev/mandelboxes/base/main/run-fractal-server.sh#L13).

### Passing environment variables

Environment variables can be passed to the docker container via [env configs](https://github.com/fractal/fractal/blob/dev/host-service/host-service.go#L233). These environment variables can be accessed by [entrypoint.sh](https://github.com/fractal/fractal/blob/dev/mandelboxes/base/startup/entrypoint.sh), [run-fractal-server.sh](https://github.com/fractal/fractal/blob/dev/mandelboxes/base/main/run-fractal-server.sh#L13) and on the mandelbox.

### Private directory

Occasionally, sensitive information needs to be saved on the mandelbox and keeping the values in a file or environment variable is not ideal. The information can be stored in `/usr/share/fractal/private/` directory with strict permissions and accessed later by a user with the correct permissions.

As a reference, aes_key is stored into a private file in [entrypoint.sh](https://github.com/fractal/fractal/blob/dev/mandelboxes/base/startup/entrypoint.sh#L14) and later accessed in [run-fractal-server.sh](https://github.com/fractal/fractal/blob/dev/mandelboxes/base/main/run-fractal-server.sh#L11).

### Writing methods for the mandelbox struct

When writing code that interacts with the mandelbox struct fields, there are some rules and guidelines that should be kept in mind. This is because the mandelbox methods use a lock for accessing the field values, and by writing code that follows the guidelines we can avoid causing unnecessary deadlocks while keeping the concurrent code easy to understand. By doing this, we will have to think less about when to lock and prevent deadlocks from conflicting locking (i.e. by using a locking getter inside a method that already locks). The following steps outline the simple process to write mandelbox code:

1. Are you accessing a mandelbox struct field directly? If so, lock.
2. Are you calling a method? If so, do not lock or you may cause a deadlock.
3. Are you inside a method? If so, assume the lock is unlocked on method entry.

Additionally, consider the following rules:

- All locking should be done when and where the actual data access occurs. This means at the point of accessing the actual struct field
- No method should assume that the lock is locked before entry
- Getters and setters should always be used for struct field access when possible

#### Miscellaneous

An alternative to resource mapping is to mount data onto the mandelbox as seen in [host-service.go](https://github.com/fractal/fractal/blob/dev/host-service/host-service.go#L564).

Another important fact to note is `fractal-startup.sh` will wait until a `.ready` file is created which happens in [host-service.go](https://github.com/fractal/fractal/blob/dev/host-service/host-service.go#L728) using the mandelbox function [MarkReady](https://github.com/fractal/fractal/blob/dev/host-service/mandelbox/mandelbox_params.go#L50).
