# Fractal Container Testing Scripts

This subfolder contains various scripts for manually testing different components of the Fractal Docker containers (i.e. latency, connectivity, etc.). If you find yourself creating a testing script while implementing or debugging a feature of the Fractal containers, add it here so that it can help others eventually.

## Existing Scripts

### connection_tester.py

Attempts to send UDP packets from client to host and records the delay, for troubleshooting whether timeouts could be an issue in connection quality.

Usage:

```
# Client side
python3 connection_tester.py client [SERVER IP]

# Server side
python3 connection_tester.py server
```

## uinput_tester.py
Creates virtual device nodes using `/dev/uinput` in order to test the uinput driver. Contains code that periodically writes to the virtual device node, which is useful for testing if the xserver running on the container is reading from the device files. Also contains code that sets up a unix socket, which is useful for testing if the container is able to communicate with the host via a socket.

Usage:

```
pip3 install python-uinput
python3 uinput_tester.py
```

## uinput_server.c
More feature-ful version of `uinput_tester.py` that performs all the steps that the host service would need to do in order for the uinput driver to work. The script is based on `uinput_input_driver.c`. It creates virtual devices using `/dev/uinput`, then waits for the container to initialize a unix socket at `/tmp/sockets/uinput.sock`. Once it detects the file is present, it connects to the socket as a client and passes the file descriptors of the virtual devices over to the container.

Usage:

```
mkdir /tmp/sockets
make
sudo ./uinput_server
```
