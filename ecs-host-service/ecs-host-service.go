package main

import (
	"math"
	"regexp"
	"strings"

	// NOTE: The "fmt" or "log" packages should never be imported!!! This is so
	// that we never forget to send a message via Sentry. Instead, use the
	// fractallogger package imported below as `logger`
	"context"
	"io"
	"math/rand"
	"os"
	"os/exec"
	"os/signal"
	"sync"
	"syscall"
	"time"

	// We use this package instead of the standard library log so that we never
	// forget to send a message via Sentry.  For the same reason, we make sure
	// not to import the fmt package either, instead separating required
	// functionality in this imported package as well.
	logger "github.com/fractal/fractal/ecs-host-service/fractallogger"

	"github.com/fractal/fractal/ecs-host-service/ecsagent"
	"github.com/fractal/fractal/ecs-host-service/fractalcontainer"
	"github.com/fractal/fractal/ecs-host-service/fractalcontainer/portbindings"
	"github.com/fractal/fractal/ecs-host-service/httpserver"
	"github.com/fractal/fractal/ecs-host-service/utils"

	dockertypes "github.com/docker/docker/api/types"
	dockercontainer "github.com/docker/docker/api/types/container"
	dockerevents "github.com/docker/docker/api/types/events"
	dockerfilters "github.com/docker/docker/api/types/filters"
	"github.com/docker/docker/api/types/strslice"
	dockerclient "github.com/docker/docker/client"
	dockernat "github.com/docker/go-connections/nat"
	dockerunits "github.com/docker/go-units"
)

func init() {
	// Initialize random number generator for all subpackages
	rand.Seed(time.Now().UnixNano())
}

// Start the Docker daemon ourselves, to have control over all Docker containers spun
func startDockerDaemon(globalCancel context.CancelFunc) {
	cmd := exec.Command("/usr/bin/systemctl", "start", "docker")
	err := cmd.Run()
	if err != nil {
		logger.Panicf(globalCancel, "Unable to start Docker daemon. Error: %v", err)
	} else {
		logger.Info("Successfully started the Docker daemon ourselves.")
	}
}

// We take ownership of the ECS agent ourselves
func startECSAgent(globalCtx context.Context, globalCancel context.CancelFunc, goroutineTracker *sync.WaitGroup) {
	goroutineTracker.Add(1)
	go func() {
		defer goroutineTracker.Done()
		exitCode := ecsagent.Main(globalCtx, globalCancel, goroutineTracker)

		// If we got here, then that means that the ecsagent has exited for some
		// reason (that means the context we passed in was cancelled, or there was
		// some initialization error). Regardless, we "panic" and cancel the
		// context if the error is nonzero.
		if exitCode != 0 {
			logger.Panicf(globalCancel, "ECS Agent exited with code %d", exitCode)
		} else {
			// Don't send error to Sentry
			globalCancel()
			logger.Infof("ECS Agent exited with code 0")
		}

	}()
}

// ------------------------------------
// Container event handlers
// ------------------------------------

// SpinUpContainer is currently only used in the localdev environment, but
// should now be "production-ready", i.e. create containers with the same (or
// equivalent) configurations as the ecsagent with our task definitions.
func SpinUpContainer(globalCtx context.Context, globalCancel context.CancelFunc, goroutineTracker *sync.WaitGroup, dockerClient *dockerclient.Client, req *httpserver.SpinUpContainerRequest) {
	logAndReturnError := func(fmt string, v ...interface{}) {
		err := logger.MakeError("SpinUpContainer(): "+fmt, v...)
		logger.Error(err)
		req.ReturnResult("", err)
	}

	// We begin by creating the container.

	fractalID := fractalcontainer.FractalID(utils.RandHex(30))
	fc := fractalcontainer.New(globalCtx, goroutineTracker, fractalID)
	logger.Infof("SpinUpContainer(): created FractalContainer object %s", fc.GetFractalID())

	// If the creation of the container fails, we want to clean up after it. We
	// do this by setting `createFailed` to true until all steps are done, and
	// closing the container's context on function exit if `createFailed` is
	// still set to true.
	var createFailed bool = true
	defer func() {
		if createFailed {
			fc.Close()
		}
	}()

	// Request port bindings for the container.
	if err := fc.AssignPortBindings([]portbindings.PortBinding{
		{ContainerPort: 32262, HostPort: 0, BindIP: "", Protocol: "tcp"},
		{ContainerPort: 32263, HostPort: 0, BindIP: "", Protocol: "udp"},
		{ContainerPort: 32273, HostPort: 0, BindIP: "", Protocol: "tcp"},
	}); err != nil {
		logAndReturnError("Error assigning port bindings: %s", err)
		return
	}
	logger.Infof("SpinUpContainer(): successfully assigned port bindings %v", fc.GetPortBindings())

	hostPortForTCP32262, err32262 := fc.GetHostPort(32262, portbindings.TransportProtocolTCP)
	hostPortForUDP32263, err32263 := fc.GetHostPort(32263, portbindings.TransportProtocolUDP)
	hostPortForTCP32273, err32273 := fc.GetHostPort(32273, portbindings.TransportProtocolTCP)
	if err32262 != nil || err32263 != nil || err32273 != nil {
		logAndReturnError("Couldn't return host port bindings.")
		return
	}

	// Initialize Uinput devices for the container
	if err := fc.InitializeUinputDevices(goroutineTracker); err != nil {
		logAndReturnError("Error initializing uinput devices: %s", err)
		return
	}
	logger.Infof("SpinUpContainer(): successfully initialized uinput devices.")
	devices := fc.GetDeviceMappings()

	// Allocate a TTY for the container
	if err := fc.InitializeTTY(); err != nil {
		logAndReturnError("Error initializing TTY: %s", err)
		return
	}
	logger.Infof("SpinUpContainer(): successfully initialized TTY.")

	appName := fractalcontainer.AppName(utils.FindSubstringBetween(req.AppImage, "fractal/", ":"))

	logger.Infof(`SpinUpContainer(): app name: "%s"`, appName)
	logger.Infof(`SpinUpContainer(): app image: "%s"`, req.AppImage)

	// We now create the underlying docker container.
	exposedPorts := make(dockernat.PortSet)
	exposedPorts[dockernat.Port("32262/tcp")] = struct{}{}
	exposedPorts[dockernat.Port("32263/udp")] = struct{}{}
	exposedPorts[dockernat.Port("32273/tcp")] = struct{}{}

	// TODO: refactor client app to no longer use webserver's AES KEY
	// https://github.com/fractal/fractal/issues/2478
	aesKey := utils.RandHex(16)
	envs := []string{
		logger.Sprintf("FRACTAL_AES_KEY=%s", aesKey),
		logger.Sprintf("WEBSERVER_URL=%s", logger.GetFractalWebserver()),
		"NVIDIA_DRIVER_CAPABILITIES=all",
		"NVIDIA_VISIBLE_DEVICES=all",
		logger.Sprintf("SENTRY_ENV=%s", logger.GetAppEnvironment()),
	}
	config := dockercontainer.Config{
		ExposedPorts: exposedPorts,
		Env:          envs,
		Image:        req.AppImage,
		AttachStdin:  true,
		AttachStdout: true,
		AttachStderr: true,
		Tty:          true,
	}
	natPortBindings := make(dockernat.PortMap)
	natPortBindings[dockernat.Port("32262/tcp")] = []dockernat.PortBinding{{HostPort: logger.Sprintf("%v", hostPortForTCP32262)}}
	natPortBindings[dockernat.Port("32263/udp")] = []dockernat.PortBinding{{HostPort: logger.Sprintf("%v", hostPortForUDP32263)}}
	natPortBindings[dockernat.Port("32273/tcp")] = []dockernat.PortBinding{{HostPort: logger.Sprintf("%v", hostPortForTCP32273)}}

	tmpfs := make(map[string]string)
	tmpfs["/run"] = "size=52428800"
	tmpfs["/run/lock"] = "size=52428800"

	hostConfig := dockercontainer.HostConfig{
		Binds: []string{
			"/sys/fs/cgroup:/sys/fs/cgroup:ro",
			logger.Sprintf("/fractal/%s/containerResourceMappings:/fractal/resourceMappings:ro", fc.GetFractalID()),
			logger.Sprintf("/fractal/temp/%s/sockets:/tmp/sockets", fc.GetFractalID()),
			"/run/udev/data:/run/udev/data:ro",
			logger.Sprintf("/fractal/%s/userConfigs/unpacked_configs:/fractal/userConfigs:rshared", fc.GetFractalID()),
		},
		PortBindings: natPortBindings,
		CapDrop:      strslice.StrSlice{"ALL"},
		CapAdd: strslice.StrSlice([]string{
			"SETPCAP",
			"MKNOD",
			"AUDIT_WRITE",
			"CHOWN",
			"NET_RAW",
			"DAC_OVERRIDE",
			"FOWNER",
			"FSETID",
			"KILL",
			"SETGID",
			"SETUID",
			"NET_BIND_SERVICE",
			"SYS_CHROOT",
			"SETFCAP",
			// NOTE THAT CAP_SYS_NICE IS NOT ENABLED BY DEFAULT BY DOCKER --- THIS IS OUR DOING
			"SYS_NICE",
		}),
		ShmSize: 2147483648,
		Tmpfs:   tmpfs,
		Resources: dockercontainer.Resources{
			CPUShares: 2,
			Memory:    6552550944,
			NanoCPUs:  0,
			// Don't need to set CgroupParent, since each container is its own task.
			// We're not using anything like AWS services, where we'd want to put
			// several containers under one limit.
			Devices:            devices,
			KernelMemory:       0,
			KernelMemoryTCP:    0,
			MemoryReservation:  0,
			MemorySwap:         0,
			Ulimits:            []*dockerunits.Ulimit{},
			CPUCount:           0,
			CPUPercent:         0,
			IOMaximumIOps:      0,
			IOMaximumBandwidth: 0,
		},
	}
	// TODO: investigate whether putting all GPUs in all containers (i.e. the default here) is beneficial.
	containerName := logger.Sprintf("%s-%s", req.AppImage, fractalID)
	re := regexp.MustCompile(`[^a-zA-Z0-9_.-]`)
	containerName = re.ReplaceAllString(containerName, "-")

	dockerBody, err := dockerClient.ContainerCreate(fc.GetContext(), &config, &hostConfig, nil, containerName)
	if err != nil {
		logAndReturnError("Error running `docker create` for %s:\n%s", fc.GetFractalID(), err)
		return
	}

	logger.Infof("Value returned from ContainerCreate: %#v", dockerBody)
	dockerID := fractalcontainer.DockerID(dockerBody.ID)

	logger.Infof("SpinUpContainer(): Successfully ran `docker create` command and got back DockerID %s", dockerID)

	err = fc.RegisterCreation(dockerID, appName)
	if err != nil {
		logAndReturnError("Error registering container creation with DockerID %s and AppName %s: %s", dockerID, appName, err)
		return
	}
	logger.Infof("SpinUpContainer(): Successfully registered container creation with DockerID %s and AppName %s", dockerID, appName)

	if err := fc.WriteResourcesForProtocol(); err != nil {
		logAndReturnError("Error writing resources for protocol: %s", err)
		return
	}
	logger.Infof("SpinUpContainer(): Successfully wrote resources for protocol.")

	err = fc.WriteLocalDevValues(req.ProtocolTimeout)
	if err != nil {
		logAndReturnError(err.Error())
		return
	}

	err = dockerClient.ContainerStart(fc.GetContext(), string(dockerID), dockertypes.ContainerStartOptions{})
	if err != nil {
		logAndReturnError("Error start container with dockerID %s and FractalID %s: %s", dockerID, fractalID, err)
		return
	}
	logger.Infof("SpinUpContainer(): Successfully started container %s", containerName)

	result := httpserver.SpinUpContainerRequestResult{
		HostPortForTCP32262: hostPortForTCP32262,
		HostPortForUDP32263: hostPortForUDP32263,
		HostPortForTCP32273: hostPortForTCP32273,
		AesKey:              aesKey,
	}

	// Mark container creation as successful, preventing cleanup on function
	// termination.
	createFailed = false

	req.ReturnResult(result, nil)
	logger.Infof("SpinUpContainer(): Finished starting up container %s", fc.GetFractalID())
}

// Handles the set config encryption token request from the client app. Takes
// the request to the `set_config_encryption_token` endpoint as an argument and
// returns nil if no errors, and error object if error.
func handleSetConfigEncryptionTokenRequest(globalCtx context.Context, globalCancel context.CancelFunc, goroutineTracker *sync.WaitGroup, req *httpserver.SetConfigEncryptionTokenRequest) {
	logAndReturnError := func(fmt string, v ...interface{}) {
		err := logger.MakeError("handleSetConfigEncryptionTokenRequest(): "+fmt, v...)
		logger.Error(err)
		req.ReturnResult("", err)
	}

	// Verify that the requested host port is valid
	if req.HostPort > math.MaxUint16 || req.HostPort < 0 {
		logAndReturnError("Invalid HostPort for set config encryption token request: %v", req.HostPort)
		return
	}
	hostPort := uint16(req.HostPort)

	fc, err := fractalcontainer.LookUpByIdentifyingHostPort(hostPort)
	if err != nil {
		logAndReturnError(err.Error())
		return
	}

	// Save config encryption token in container struct
	fc.SetConfigEncryptionToken(fractalcontainer.ConfigEncryptionToken(req.ConfigEncryptionToken))

	// If CompleteContainerSetup doesn't verify the client app access token, configs are not retrieved or saved.
	err = fc.CompleteContainerSetup(fractalcontainer.UserID(req.UserID), fractalcontainer.ClientAppAccessToken(req.ClientAppAccessToken), "handleSetConfigEncryptionTokenRequest")
	if err != nil {
		logAndReturnError(err.Error())
		return
	}

	req.ReturnResult("", nil)
}

// Creates a file containing the DPI assigned to a specific container, and make
// it accessible to that container. Also take the received User ID and retrieve
// the user's app configs if the User ID is set. We make this function send
// back the result for the provided request so that we can run in its own
// goroutine and not block the event loop goroutine.
func handleStartValuesRequest(globalCtx context.Context, globalCancel context.CancelFunc, goroutineTracker *sync.WaitGroup, req *httpserver.SetContainerStartValuesRequest) {
	logAndReturnError := func(fmt string, v ...interface{}) {
		err := logger.MakeError("handleStartValuesRequest(): "+fmt, v...)
		logger.Error(err)
		req.ReturnResult("", err)
	}

	// Verify identifying hostPort value
	if req.HostPort > math.MaxUint16 || req.HostPort < 0 {
		logAndReturnError("Invalid HostPort: %v", req.HostPort)
		return
	}
	hostPort := uint16(req.HostPort)

	fc, err := fractalcontainer.LookUpByIdentifyingHostPort(hostPort)
	if err != nil {
		logAndReturnError(err.Error())
		return
	}

	err = fc.WriteStartValues(req.DPI, req.ContainerARN)
	if err != nil {
		logAndReturnError(err.Error())
		return
	}

	err = fc.CompleteContainerSetup(fractalcontainer.UserID(req.UserID), fractalcontainer.ClientAppAccessToken(req.ClientAppAccessToken), fractalcontainer.SetupEndpoint("handleStartValuesRequest"))
	if err != nil {
		logAndReturnError(err.Error())
		return
	}

	req.ReturnResult("", nil)
}

// Handle tasks to be completed when a container dies
func containerDieHandler(id string) {
	// Exit if we are not dealing with a Fractal container, or if it has already
	// been closed (via a call to Close() or a context cancellation).
	fc, err := fractalcontainer.LookUpByDockerID(fractalcontainer.DockerID(id))
	if err != nil {
		logger.Infof("containerDieHandler(): %s", err)
		return
	}

	fc.Close()
}

// ---------------------------
// Service shutdown and initialization
// ---------------------------

// Create the directory used to store the container resource allocations
// (e.g. TTYs) on disk
func initializeFilesystem(globalCancel context.CancelFunc) {
	// check if "/fractal" already exists --- if so, panic, since
	// we don't know why it's there or if it's valid
	if _, err := os.Lstat(utils.FractalDir); !os.IsNotExist(err) {
		if err == nil {
			logger.Panicf(globalCancel, "Directory %s already exists!", utils.FractalDir)
		} else {
			logger.Panicf(globalCancel, "Could not make directory %s because of error %v", utils.FractalDir, err)
		}
	}

	// Create the fractal directory and make it non-root user owned so that
	// non-root users in containers can access files within (especially user
	// configs). We do this in a deferred function so that any subdirectories
	// created later in this function are also covered.
	err := os.MkdirAll(utils.FractalDir, 0777)
	if err != nil {
		logger.Panicf(globalCancel, "Failed to create directory %s: error: %s\n", utils.FractalDir, err)
	}
	defer func() {
		cmd := exec.Command("chown", "-R", "ubuntu", utils.FractalDir)
		cmd.Run()
	}()

	// Create fractal-private directory
	err = os.MkdirAll(httpserver.FractalPrivatePath, 0777)
	if err != nil {
		logger.Panicf(globalCancel, "Failed to create directory %s: error: %s\n", httpserver.FractalPrivatePath, err)
	}

	// Create fractal temp directory
	err = os.MkdirAll(utils.TempDir, 0777)
	if err != nil {
		logger.Panicf(globalCancel, "Could not mkdir path %s. Error: %s", utils.TempDir, err)
	}
}

// Delete the directory used to store the container resource allocations (e.g.
// TTYs) on disk, as well as the directory used to store the SSL certificate we
// use for the httpserver, and our temporary directory.
func uninitializeFilesystem() {
	err := os.RemoveAll(utils.FractalDir)
	if err != nil {
		logger.Errorf("Failed to delete directory %s: error: %v\n", utils.FractalDir, err)
	} else {
		logger.Infof("Successfully deleted directory %s\n", utils.FractalDir)
	}

	err = os.RemoveAll(httpserver.FractalPrivatePath)
	if err != nil {
		logger.Errorf("Failed to delete directory %s: error: %v\n", httpserver.FractalPrivatePath, err)
	} else {
		logger.Infof("Successfully deleted directory %s\n", httpserver.FractalPrivatePath)
	}

	err = os.RemoveAll(utils.TempDir)
	if err != nil {
		logger.Errorf("Failed to delete directory %s: error: %v\n", utils.TempDir, err)
	} else {
		logger.Infof("Successfully deleted directory %s\n", utils.TempDir)
	}
}

func main() {
	// The host service needs root permissions.
	logger.RequireRootPermissions()

	// We create a global context (i.e. for the entire host service) that can be
	// cancelled if the entire program needs to terminate. We also create a
	// WaitGroup for all goroutines to tell us when they've stopped (if the
	// context gets cancelled). Finally, we defer a function which cancels the
	// global context if necessary, logs any panic we might be recovering from,
	// and cleans up after the entire host service (although some aspects of
	// cleanup may not seem necessary on a production instance, they are
	// immensely helpful to prevent the host service from clogging up the local
	// filesystem during development). After the permissions check, the creation
	// of this context and WaitGroup, and the following defer must be the first
	// statements in main().
	globalCtx, globalCancel := context.WithCancel(context.Background())
	goroutineTracker := sync.WaitGroup{}
	defer func() {
		// This function cleanly shuts down the Fractal ECS host service. Note that
		// besides the host machine itself shutting down, this deferred function
		// from main() should be the _only_ way that the host service exits. In
		// particular, it should be as a result of a panic() in main, the global
		// context being cancelled, or a Ctrl+C interrupt.

		// Note that this function, while nontrivial, has intentionally been left
		// as part of main() for the following reasons:
		//   1. To prevent it being called from anywhere else accidentally
		//   2. To keep the entire program control flow clearly in main().

		// Catch any panics that might have originated in main() or one of its
		// direct children.
		r := recover()
		if r != nil {
			logger.Infof("Shutting down host service after caught panic in main(): %v", r)
		} else {
			logger.Infof("Beginning host service shutdown procedure...")
		}

		// Cancel the global context, if it hasn't already been cancelled. Note
		// that this also cleans up after every container.
		globalCancel()

		// Wait for all goroutines to stop, so we can run the rest of the cleanup
		// process.
		goroutineTracker.Wait()

		// Shut down the logging infrastructure.
		logger.Close()

		uninitializeFilesystem()

		logger.Info("Finished host service shutdown procedure. Finally exiting...")
		os.Exit(0)
	}()

	// Log the Git commit of the running executable
	logger.Info("Host Service Version: %s", logger.GetGitCommit())

	initializeFilesystem(globalCancel)

	// Now we start all the goroutines that actually do work.

	// Start the HTTP server and listen for events
	httpServerEvents, err := httpserver.Start(globalCtx, globalCancel, &goroutineTracker)
	if err != nil {
		logger.Panic(globalCancel, err)
	}

	startDockerDaemon(globalCancel)

	// Only start the ECS Agent if we are talking to a dev, staging, or
	// production webserver.
	if logger.GetAppEnvironment() != logger.EnvLocalDev {
		logger.Infof("Talking to the %v webserver -- starting ECS Agent.", logger.GetAppEnvironment())
		startECSAgent(globalCtx, globalCancel, &goroutineTracker)
	} else {
		logger.Infof("Running in environment LocalDev, so not starting ecs-agent.")
	}

	// Start main event loop
	startEventLoop(globalCtx, globalCancel, &goroutineTracker, httpServerEvents)

	// Register a signal handler for Ctrl-C so that we cleanup if Ctrl-C is pressed.
	sigChan := make(chan os.Signal, 2)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)

	// Wait for either the global context to get cancelled by a worker goroutine,
	// or for us to receive an interrupt. This needs to be the end of main().
	select {
	case <-sigChan:
		logger.Infof("Got an interrupt or SIGTERM")
	case <-globalCtx.Done():
		logger.Infof("Global context cancelled!")
	}
}

func startEventLoop(globalCtx context.Context, globalCancel context.CancelFunc, goroutineTracker *sync.WaitGroup, httpServerEvents <-chan httpserver.ServerRequest) {
	goroutineTracker.Add(1)
	go func() {
		defer goroutineTracker.Done()

		// Create docker client
		dockerClient, err := dockerclient.NewClientWithOpts(dockerclient.WithAPIVersionNegotiation())
		if err != nil {
			logger.Panicf(globalCancel, "Error creating new Docker client: %v", err)
		}

		// Create filter for which docker events we care about
		filters := dockerfilters.NewArgs()
		filters.Add("type", dockerevents.ContainerEventType)
		eventOptions := dockertypes.EventsOptions{
			Filters: filters,
		}

		// In the following loop, this var determines whether to re-initialize the
		// Docker event stream. This is necessary because the Docker event stream
		// needs to be reopened after any error is sent over the error channel.
		needToReinitDockerEventStream := false
		dockerevents, dockererrs := dockerClient.Events(globalCtx, eventOptions)
		logger.Info("Initialized docker event stream.")
		logger.Info("Entering event loop...")

		// The actual event loop
		for {
			if needToReinitDockerEventStream {
				dockerevents, dockererrs = dockerClient.Events(globalCtx, eventOptions)
				needToReinitDockerEventStream = false
				logger.Info("Re-initialized docker event stream.")
			}

			select {
			case <-globalCtx.Done():
				logger.Infof("Leaving main event loop...")
				return

			case err := <-dockererrs:
				needToReinitDockerEventStream = true
				switch {
				case err == nil:
					logger.Info("We got a nil error over the Docker event stream. This might indicate an error inside the docker go library. Ignoring it and proceeding normally...")
					continue
				case err == io.EOF:
					logger.Panicf(globalCancel, "Docker event stream has been completely read.")
				case dockerclient.IsErrConnectionFailed(err):
					// This means "Cannot connect to the Docker daemon..."
					logger.Info("Got error \"%v\". Trying to start Docker daemon ourselves...", err)
					startDockerDaemon(globalCancel)
					continue
				default:
					if !strings.HasSuffix(strings.ToLower(err.Error()), "context canceled") {
						logger.Panicf(globalCancel, "Got an unknown error from the Docker event stream: %v", err)
					}
					return
				}

			case dockerevent := <-dockerevents:
				if dockerevent.Action == "die" || dockerevent.Action == "start" {
					logger.Info("dockerevent: %s for %s %s\n", dockerevent.Action, dockerevent.Type, dockerevent.ID)
				}
				if dockerevent.Action == "die" {
					containerDieHandler(dockerevent.ID)
				}

			// It may seem silly to just launch goroutines to handle these
			// serverevents, but we aim to keep the high-level flow control and handling
			// in this package, and the low-level authentication, parsing, etc. of
			// requests in `httpserver`.
			case serverevent := <-httpServerEvents:
				switch serverevent.(type) {
				// TODO: actually handle panics in these goroutines
				case *httpserver.SetContainerStartValuesRequest:
					go handleStartValuesRequest(globalCtx, globalCancel, goroutineTracker, serverevent.(*httpserver.SetContainerStartValuesRequest))

				case *httpserver.SetConfigEncryptionTokenRequest:
					go handleSetConfigEncryptionTokenRequest(globalCtx, globalCancel, goroutineTracker, serverevent.(*httpserver.SetConfigEncryptionTokenRequest))

				case *httpserver.SpinUpContainerRequest:
					go SpinUpContainer(globalCtx, globalCancel, goroutineTracker, dockerClient, serverevent.(*httpserver.SpinUpContainerRequest))

				default:
					if serverevent != nil {
						err := logger.MakeError("unimplemented handling of server event [type: %T]: %v", serverevent, serverevent)
						logger.Error(err)
						serverevent.ReturnResult("", err)
					}
				}

			}
		}
	}()
}
