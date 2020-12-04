## Fractal Desktop Clients

This folder builds a client to receive a server stream via Fractal. It supports Windows, MacOS and Linux Ubuntu. See below for basic build instructions.

### Development

You can run `desktop.bat` (Windows) or `desktop.sh` to restart your dev environment.

To compile, you should first run `cmake .` (MacOS/Linux) or `cmake -G "NMake Makefiles"` (Windows) from the root directory, `/protocol/`. You can then cd into this folder and run `make` (MacOS/Linux) or `nmake` (Windows).

If you're having trouble compiling, make sure that you followed the instructions in the root-level README. If still having issue, you should delete the CmakeCache or start from a fresh repository.

### Running

From the `/desktop` directory, you can simply run:

```
Windows:
desktop IP_ADDRESS [OPTION]...

MacOS/Linux:
./desktop IP_ADDRESS [OPTION]...
```

The option flags are as follows:

```
  -w, --width=WIDTH             set the width for the windowed-mode
                                  window, if both width and height
                                  are specified
  -h, --height=HEIGHT           set the height for the windowed-mode
                                  window, if both width and height
                                  are specified
  -b, --bitrate=BITRATE         set the maximum bitrate to use
  -s, --spectate                launch the protocol as a spectator
  -c, --codec=CODEC             launch the protocol using the codec
                                  specified: h264 (default) or h265
  -u, --user                    Tell fractal the users email, optional defaults to None"
  -e, --environment             The environment the protocol is running
                                in. e.g master, staging, dev. This is used
                                for sentry. Optional defaults to dev
  --help     display this help and exit"
  --version  output version information and exit;
```

For example, to run the protocol on IP address `0.0.0.0` in an `800x600` window on Linux, call:

```
./desktop 0.0.0.0 --width 800 --height 600
```

Alternatively, if you want to run the executable directly, in the `/desktop/build64/[Windows/Darwin/Linux]` directory or `/desktop/build32/Windows`, run:

```
Windows:
FractalClient [OPTION]... IP ADDRESS

MacOS/Linux:
./FractalClient [OPTION]... IP ADDRESS
```
