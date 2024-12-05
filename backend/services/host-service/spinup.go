package main

import (
	"context"
	"crypto/sha1"
	"os"
	"path"
	"regexp"
	"strconv"
	"sync"
	"time"

	dockertypes "github.com/docker/docker/api/types"
	dockercontainer "github.com/docker/docker/api/types/container"
	"github.com/docker/docker/api/types/strslice"
	dockerclient "github.com/docker/docker/client"
	dockernat "github.com/docker/go-connections/nat"
	dockerunits "github.com/docker/go-units"
	v1 "github.com/opencontainers/image-spec/specs-go/v1"
	"github.com/whisthq/whist/backend/services/host-service/dbdriver"
	mandelboxData "github.com/whisthq/whist/backend/services/host-service/mandelbox"
	"github.com/whisthq/whist/backend/services/host-service/mandelbox/portbindings"
	"github.com/whisthq/whist/backend/services/host-service/metrics"
	"github.com/whisthq/whist/backend/services/metadata"
	mandelboxtypes "github.com/whisthq/whist/backend/services/types"
	"github.com/whisthq/whist/backend/services/utils"
	logger "github.com/whisthq/whist/backend/services/whistlogger"
	"go.uber.org/zap"
	"golang.org/x/sync/errgroup"
)

// SpinUpMandelbox will create and start a mandelbox. Once the mandelbox is started, it effectively waits an infinite time until a user gets assigned to it.
func SpinUpMandelbox(globalCtx context.Context, goroutineTracker *sync.WaitGroup, dockerClient dockerclient.CommonAPIClient, mandelboxID mandelboxtypes.MandelboxID, appName mandelboxtypes.AppName, mandelboxDieChan chan bool, kioskMode bool, loadExtension bool, localClient bool) (mandelboxData.Mandelbox, error) {
	incrementErrorRate := func() {
		metrics.Increment("ErrorRate")
	}

	mandelbox := mandelboxData.New(context.Background(), goroutineTracker, mandelboxID, mandelboxDieChan)
	logger.Infof("SpinUpMandelbox(): created Mandelbox object %s", mandelbox.GetID())

	contextFields := []interface{}{
		zap.String("mandelbox_id", mandelbox.GetID().String()),
	}

	// If the creation of the mandelbox fails, we want to clean up after it. We
	// do this by setting `createFailed` to true until all steps are done, and
	// closing the mandelbox's context on function exit if `createFailed` is
	// still set to true.
	var createFailed bool = true
	defer func() {
		if createFailed {
			mandelbox.Close()
		}
	}()

	mandelbox.SetAppName(appName)

	// Generate a random hash to set as the server-side session id.
	// This session ID is used for the mandelbox services that start
	// before assigning a user.
	randBytes := make([]byte, 64)
	hash := sha1.New()
	hash.Write(randBytes)
	serverSessionID := utils.Sprintf("%x", hash.Sum(nil))
	mandelbox.SetSessionID(mandelboxtypes.SessionID(serverSessionID))

	// Do all startup tasks that can be done before Docker container creation in
	// parallel, stopping at the first error encountered
	preCreateGroup, _ := errgroup.WithContext(mandelbox.GetContext())

	var hostPortForTCP32261, hostPortForTCP32262, hostPortForUDP32263, hostPortForTCP32273 uint16
	preCreateGroup.Go(func() error {
		if err := mandelbox.AssignPortBindings([]portbindings.PortBinding{
			{MandelboxPort: 32261, HostPort: 0, BindIP: "", Protocol: "tcp"},
			{MandelboxPort: 32262, HostPort: 0, BindIP: "", Protocol: "tcp"},
			{MandelboxPort: 32263, HostPort: 0, BindIP: "", Protocol: "udp"},
			{MandelboxPort: 32273, HostPort: 0, BindIP: "", Protocol: "tcp"},
		}); err != nil {
			return utils.MakeError("error assigning port bindings: %s", err)
		}
		logger.Infow(utils.Sprintf("SpinUpMandelbox(): successfully assigned port bindings %v", mandelbox.GetPortBindings()), contextFields)
		// Request port bindings for the mandelbox.
		var err32261, err32262, err32263, err32273 error
		hostPortForTCP32261, err32261 = mandelbox.GetHostPort(32261, portbindings.TransportProtocolTCP)
		hostPortForTCP32262, err32262 = mandelbox.GetHostPort(32262, portbindings.TransportProtocolTCP)
		hostPortForUDP32263, err32263 = mandelbox.GetHostPort(32263, portbindings.TransportProtocolUDP)
		hostPortForTCP32273, err32273 = mandelbox.GetHostPort(32273, portbindings.TransportProtocolTCP)
		if err32261 != nil || err32262 != nil || err32263 != nil || err32273 != nil {
			incrementErrorRate()
			return utils.MakeError("couldn't return host port bindings")
		}
		return nil
	})

	// Initialize Uinput devices for the mandelbox
	var devices []dockercontainer.DeviceMapping
	preCreateGroup.Go(func() error {
		if err := mandelbox.InitializeUinputDevices(goroutineTracker); err != nil {
			return utils.MakeError("error initializing uinput devices: %s", err)
		}

		logger.Infow("SpinUpMandelbox(): successfully initialized uinput devices.", contextFields)
		devices = mandelbox.GetDeviceMappings()
		return nil
	})

	// Allocate a TTY and GPU for the mandelbox
	preCreateGroup.Go(func() error {
		if err := mandelbox.InitializeTTY(); err != nil {
			return utils.MakeError("error initializing TTY: %s", err)
		}

		// CI does not have GPUs
		if !metadata.IsRunningInCI() && metadata.IsGPU() {
			if err := mandelbox.AssignGPU(); err != nil {
				return utils.MakeError("error assigning GPU: %s", err)
			}
		}

		logger.Infow(utils.Sprintf("SpinUpMandelbox(): successfully allocated TTY %v and assigned GPU %v", mandelbox.GetTTY(), mandelbox.GetGPU()), contextFields)
		return nil
	})

	err := preCreateGroup.Wait()
	if err != nil {
		incrementErrorRate()
		return mandelbox, err
	}

	// We need to compute the Docker image to use for this mandelbox. In local
	// development, we want to accept any string so that we can run arbitrary
	// containers as mandelboxes, including custom-tagged ones. However, in
	// deployments we only want to accept the specific apps that are enabled, and
	// don't want to leak the fact that we're using containers as the technology
	// for mandelboxes, so we construct the image string ourselves.
	var image string
	if metadata.IsLocalEnv() {

		regexes := []string{
			string(appName) + ":current-build",
			string(appName),
		}

		image = dockerImageFromRegexes(globalCtx, dockerClient, regexes)
	} else {
		image = utils.Sprintf("ghcr.io/whisthq/%s/%s:current-build", metadata.GetAppEnvironmentLowercase(), appName)
	}

	// We now create the underlying Docker container for this mandelbox.
	exposedPorts := make(dockernat.PortSet)
	exposedPorts[dockernat.Port("32261/tcp")] = struct{}{}
	exposedPorts[dockernat.Port("32262/tcp")] = struct{}{}
	exposedPorts[dockernat.Port("32263/udp")] = struct{}{}
	exposedPorts[dockernat.Port("32273/tcp")] = struct{}{}

	aesKey := utils.RandHex(16)
	mandelbox.SetPrivateKey(mandelboxtypes.PrivateKey(aesKey))

	// Common env variables
	envs := []string{
		utils.Sprintf("WHIST_AES_KEY=%s", aesKey),
		utils.Sprintf("SENTRY_ENV=%s", metadata.GetAppEnvironment()),
		utils.Sprintf("KIOSK_MODE=%t", kioskMode),
		utils.Sprintf("LOAD_EXTENSION=%t", loadExtension),
		utils.Sprintf("LOCAL_CLIENT=%t", localClient),
	}

	// Add additional env variables if host is using GPU
	if metadata.IsGPU() {
		logger.Infof("Using GPU env vars for container.")
		envs = append(envs, utils.Sprintf("NVIDIA_VISIBLE_DEVICES=%v", "all"))
		envs = append(envs, "NVIDIA_DRIVER_CAPABILITIES=all")
	}

	config := dockercontainer.Config{
		ExposedPorts: exposedPorts,
		Env:          envs,
		Image:        image,
		AttachStdin:  true,
		AttachStdout: true,
		AttachStderr: true,
		Tty:          true,
	}
	natPortBindings := make(dockernat.PortMap)
	natPortBindings[dockernat.Port("32261/tcp")] = []dockernat.PortBinding{{HostPort: utils.Sprintf("%v", hostPortForTCP32261)}}
	natPortBindings[dockernat.Port("32262/tcp")] = []dockernat.PortBinding{{HostPort: utils.Sprintf("%v", hostPortForTCP32262)}}
	natPortBindings[dockernat.Port("32263/udp")] = []dockernat.PortBinding{{HostPort: utils.Sprintf("%v", hostPortForUDP32263)}}
	natPortBindings[dockernat.Port("32273/tcp")] = []dockernat.PortBinding{{HostPort: utils.Sprintf("%v", hostPortForTCP32273)}}

	tmpfs := make(map[string]string)
	tmpfs["/run"] = "size=52428800"
	tmpfs["/run/lock"] = "size=52428800"

	hostConfig := dockercontainer.HostConfig{
		Binds: []string{
			"/sys/fs/cgroup:/sys/fs/cgroup:ro",
			path.Join(utils.WhistDir, mandelbox.GetID().String(), "mandelboxResourceMappings", ":", "/whist", "resourceMappings"),
			path.Join(utils.TempDir, mandelbox.GetID().String(), "sockets", ":", "tmp", "sockets"),
			path.Join(utils.TempDir, "logs", mandelbox.GetID().String(), serverSessionID, ":", "var", "log", "whist"), "/run/udev/data:/run/udev/data:ro",
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
			// NOTE THAT THE FOLLOWING ARE NOT ENABLED BY DEFAULT BY DOCKER --- THIS IS OUR DOING
			"SYS_NICE",
			"IPC_LOCK",
		}),
		ShmSize: 2147483648,
		Tmpfs:   tmpfs,
		Resources: dockercontainer.Resources{
			CPUShares: 2,
			Memory:    6552550944,
			NanoCPUs:  0,
			// Don't need to set CgroupParent, since each mandelbox is its own task.
			// We're not using anything like AWS services, where we'd want to put
			// several mandelboxes under one limit.
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
		SecurityOpt: []string{
			"apparmor:mandelbox-apparmor-profile",
		},
	}
	mandelboxName := utils.Sprintf("%s-%s", appName, mandelboxID)
	re := regexp.MustCompile(`[^a-zA-Z0-9_.-]`)
	mandelboxName = re.ReplaceAllString(mandelboxName, "-")

	dockerBody, err := dockerClient.ContainerCreate(mandelbox.GetContext(), &config, &hostConfig, nil, &v1.Platform{Architecture: "amd64", OS: "linux"}, mandelboxName)
	if err != nil {
		incrementErrorRate()
		return mandelbox, utils.MakeError("error running `create`: %s", err)
	}

	logger.Infow(utils.Sprintf("SpinUpMandelbox(): Value returned from ContainerCreate: %#v", dockerBody), contextFields)
	dockerID := mandelboxtypes.DockerID(dockerBody.ID)

	logger.Infow(utils.Sprintf("SpinUpMandelbox(): Successfully ran `create` command and got back runtime ID %s", dockerID), contextFields)

	postCreateGroup, _ := errgroup.WithContext(mandelbox.GetContext())

	// Register docker ID in mandelbox object
	postCreateGroup.Go(func() error {
		err := mandelbox.RegisterCreation(dockerID)
		if err != nil {
			return utils.MakeError("error registering mandelbox creation with runtime ID %s: %s", dockerID, err)
		}

		logger.Infow(utils.Sprintf("SpinUpMandelbox(): Successfully registered mandelbox creation with runtime ID %s and AppName %s", dockerID, appName), contextFields)
		return nil
	})

	// Write mandelbox parameters to file
	postCreateGroup.Go(func() error {
		if err := mandelbox.WriteMandelboxParams(); err != nil {
			return utils.MakeError("error writing mandelbox params: %s", err)
		}

		// Server protocol waits forever for a client connection and waits for 30
		// seconds for another connection after the first successful connection
		// has been disconnected. However, in localdev we default to an infinite
		// timeout to enable easier testing. In localdev environments, we can
		// override this using an environment variable.
		//
		// Since the server protocol only starts after the user configs are loaded,
		// zygote mandelboxes will wait forever until JSON transport. Once that occurs,
		// the server timeout will exit correctly if a client disconnects or fails to
		// connect.
		protocolConnectTimeout := -1
		protocolDisconnectTimeout := 30
		if metadata.IsLocalEnv() {
			localDevConnectTimeout := os.Getenv("LOCALDEV_PROTOCOL_CONNECT_TIMEOUT")
			if localDevConnectTimeout != "" {
				protocolConnectTimeout, err = strconv.Atoi(localDevConnectTimeout)
				if err != nil {
					logger.Warningf("SpinUpMandelbox(): Error parsing LOCALDEV_PROTOCOL_CONNECT_TIMEOUT envvar: %s", err)
				}
			} else {
				protocolConnectTimeout = -1
			}

			localDevDisconnectTimeout := os.Getenv("LOCALDEV_PROTOCOL_DISCONNECT_TIMEOUT")
			if localDevDisconnectTimeout != "" {
				protocolDisconnectTimeout, err = strconv.Atoi(localDevDisconnectTimeout)
				if err != nil {
					logger.Warningf("SpinUpMandelbox(): Error parsing LOCALDEV_PROTOCOL_DISCONNECT_TIMEOUT envvar: %s", err)
				}
			} else {
				protocolDisconnectTimeout = -1
			}
		}

		if err := mandelbox.WriteProtocolTimeouts(protocolConnectTimeout, protocolDisconnectTimeout); err != nil {
			return utils.MakeError("error writing protocol timeouts: %s", err)
		}

		logger.Infow("SpinUpMandelbox(): Successfully wrote parameters for mandelbox", contextFields)
		return nil
	})

	err = mandelbox.MarkParamsReady()
	if err != nil {
		incrementErrorRate()
		return mandelbox, utils.MakeError("error marking mandelbox as ready to start A/V + display: %s", err)
	}
	logger.Infow("SpinUpMandelbox(): Successfully marked mandelbox params as ready. A/V and display services can soon start.", contextFields)

	// Start Docker container
	postCreateGroup.Go(func() error {
		err = dockerClient.ContainerStart(mandelbox.GetContext(), string(dockerID), dockertypes.ContainerStartOptions{})
		if err != nil {
			return utils.MakeError("error starting mandelbox with runtime ID %s and MandelboxID %s: %s", dockerID, mandelboxID, err)
		}

		logger.Infow(utils.Sprintf("SpinUpMandelbox(): Successfully started mandelbox %s with ID %s", mandelboxName, mandelboxID), contextFields)
		return nil
	})

	// Mark as ready only when all startup tasks have completed
	err = postCreateGroup.Wait()
	if err != nil {
		incrementErrorRate()
		return mandelbox, err
	}

	logger.Info("SpinUpMandelbox(): Waiting for mandelbox whist-application to start up...")
	err = utils.WaitForFileCreation(path.Join(utils.WhistDir, mandelbox.GetID().String(), "mandelboxResourceMappings"), "done_sleeping_until_X_clients", time.Second*20, nil)
	if err != nil {
		incrementErrorRate()
		return mandelbox, utils.MakeError("error waiting for mandelbox whist-application to start: %s", err)
	}

	logger.Info("SpinUpMandelbox(): Finished waiting for mandelbox whist application to start up")

	// Mark mandelbox creation as successful, preventing cleanup on function
	// termination.
	createFailed = false

	// Mark mandelbox creation as successful, preventing cleanup on function
	// termination.
	createFailed = false
	mandelbox.SetStatus(dbdriver.MandelboxStatusWaiting)

	return mandelbox, nil
}
