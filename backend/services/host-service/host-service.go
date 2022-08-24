/*
The Whist Host-Service is responsible for orchestrating mandelboxes (i.e.
Whist-enabled containers) on EC2 instances (referred to as "hosts" throughout
the codebase). The host-service is responsible for making Docker calls to start
and stop mandelboxes, for enabling multiple mandelboxes to run concurrently on
the same host (by dynamically allocating and assigning resources), and for
passing startup data to the mandelboxes, both from the rest of the backend and
from the user's frontend application.

If you are just interested in seeing what endpoints the host-service exposes
(i.e. for frontend development), check out the file `httpserver.go`.

The main package of the host service contains the main logic and the most
comments to explain the design decisions of the host service. It also contains
an HTTPS server that exposes the necessary endpoints and sets up the necessary
infrastructure for concurrent handlers, etc.
*/
package main

import (
	// NOTE: The "fmt" or "log" packages should never be imported!!! This is so
	// that we never forget to send a message via Sentry. Instead, use the
	// whistlogger package imported below as `logger`.

	"context"
	_ "embed"
	"io"
	"math/rand"
	"os"
	"os/exec"
	"os/signal"
	"path"
	"regexp"
	"strings"
	"sync"
	"syscall"
	"time"

	// We use this package instead of the standard library log so that we never
	// forget to send a message via Sentry. For the same reason, we make sure not
	// to import the fmt package either, instead separating required
	// functionality in this imported package as well.

	"github.com/google/uuid"
	"github.com/hashicorp/go-multierror"
	"github.com/whisthq/whist/backend/services/httputils"
	logger "github.com/whisthq/whist/backend/services/whistlogger"
	"go.uber.org/zap"

	"github.com/whisthq/whist/backend/services/host-service/dbdriver"
	mandelboxData "github.com/whisthq/whist/backend/services/host-service/mandelbox"
	"github.com/whisthq/whist/backend/services/host-service/metrics"
	"github.com/whisthq/whist/backend/services/metadata"
	"github.com/whisthq/whist/backend/services/subscriptions"
	mandelboxtypes "github.com/whisthq/whist/backend/services/types"
	"github.com/whisthq/whist/backend/services/utils"

	dockertypes "github.com/docker/docker/api/types"
	dockerevents "github.com/docker/docker/api/types/events"
	dockerfilters "github.com/docker/docker/api/types/filters"
	dockerclient "github.com/docker/docker/client"
)

var shutdownInstanceOnExit bool = !metadata.IsLocalEnv()

func init() {
	// Initialize random number generator for all subpackages
	rand.Seed(time.Now().UnixNano())

	// Check that the program has been started with the correct permissions ---
	// for now, we just want to run as root, but this service could be assigned
	// its own user in the future.
	if os.Geteuid() != 0 {
		// We can do a "real" panic here because it's in an init function, so we
		// haven't even entered the host-service main() yet.
		logger.Panicf(nil, "This service needs to run as root!")
	}
}

// createDockerClient creates a docker client. It returns an error if creation
// failed.
func createDockerClient() (*dockerclient.Client, error) {
	client, err := dockerclient.NewClientWithOpts(dockerclient.WithAPIVersionNegotiation())
	if err != nil {
		return nil, utils.MakeError("error creating new Docker client: %s", err)
	}
	return client, nil
}

// Given a list of regexes, find a Docker image whose name matches the earliest
// possible regex in the list.
func dockerImageFromRegexes(globalCtx context.Context, dockerClient dockerclient.CommonAPIClient, regexes []string) string {
	imageFilters := dockerfilters.NewArgs(
		dockerfilters.KeyValuePair{Key: "dangling", Value: "false"},
	)
	images, err := dockerClient.ImageList(globalCtx, dockertypes.ImageListOptions{All: false, Filters: imageFilters})
	if err != nil {
		return ""
	}

	for _, regex := range regexes {
		rgx := regexp.MustCompile(regex)
		for _, img := range images {
			for _, tag := range img.RepoTags {
				if rgx.MatchString(tag) {
					return tag
				}
			}
		}
	}
	return ""
}

// Drain and shutdown the host-service
func drainAndShutdown(globalCtx context.Context, globalCancel context.CancelFunc, goroutineTracker *sync.WaitGroup) {
	logger.Infof("Got a DrainAndShutdownRequest... cancelling the global context.")
	shutdownInstanceOnExit = true
	globalCancel()
}

func SpinUpMandelboxes(globalCtx context.Context, globalCancel context.CancelFunc, goroutineTracker *sync.WaitGroup, dockerClient dockerclient.CommonAPIClient, mandelboxDieChan chan bool, instanceID string, instanceCapacity int32) {
	if metadata.IsLocalEnvWithoutDB() && !metadata.IsRunningInCI() {
		return
	}

	// Start all waiting mandelboxes we can (i.e. as many as we have capacity for) and register to database
	// with the "WAITING" status. The instance capacity is determined by the scaling service for each instance type.
	for i := int32(0); i < instanceCapacity; i++ {
		mandelboxID := mandelboxtypes.MandelboxID(uuid.New())
		// Replace "whistium" with "brave" (or some other container we support) to test a different app. Note that the Whist
		// backend is designed to only ever deploy the same application everywhere, which we hardcode here.
		var appName mandelboxtypes.AppName = "browsers/whistium"
		zygote, err := StartMandelboxSpinUp(globalCtx, globalCancel, goroutineTracker, dockerClient, mandelboxID, appName, mandelboxDieChan)

		// If we fail to create a zygote mandelbox, it indicates a problem with the instance, or the Docker
		// images. Its not safe to assign users to it, so we cancel the global context and shut down the instance
		if zygote == nil || err != nil {
			globalCancel()
			logger.Errorw(utils.Sprintf("failed to start waiting mandelbox: %s", err), []interface{}{
				zap.String("mandelbox_id", zygote.GetID().String()),
				zap.Time("updated_at", zygote.GetLastUpdatedTime()),
			})
			return
		}

		// We have to parse the appname before writing to the database.
		appString := strings.Split(string(zygote.GetAppName()), "/")
		appNameForDb := strings.ToUpper(appString[1])
		err = dbdriver.CreateMandelbox(zygote.GetID(), appNameForDb, instanceID)
		if err != nil {
			logger.Errorf("failed to register mandelbox %v on database. Err: %v", zygote.GetID(), err)
		}
	}
}

// Handle tasks to be completed when a mandelbox dies
func mandelboxDieHandler(id string, transportRequestMap map[mandelboxtypes.MandelboxID]chan *httputils.JSONTransportRequest, transportMapLock *sync.Mutex, dockerClient dockerclient.CommonAPIClient) {
	// Exit if we are not dealing with a Whist mandelbox, or if it has already
	// been closed (via a call to Close() or a context cancellation).
	mandelbox, err := mandelboxData.LookUpByDockerID(mandelboxtypes.DockerID(id))
	if err != nil {
		logger.Warningf("mandelboxDieHandler(): %s", err)
		return
	}

	// Clean up this user from the JSON transport request map.
	mandelboxID := mandelbox.GetID()
	transportMapLock.Lock()
	transportRequestMap[mandelboxID] = nil
	transportMapLock.Unlock()

	logger.Infof("Cancelling mandelbox context.")
	mandelbox.Close()

	// Gracefully shut down the mandelbox Docker container
	stopTimeout := 10 * time.Second
	stopCtx, cancel := context.WithCancel(context.Background())
	defer cancel()

	err = dockerClient.ContainerStop(stopCtx, id, &stopTimeout)
	if err != nil {
		logger.Warningf("Failed to gracefully stop mandelbox docker container: %s", err)
	}

}

func monitorWaitingMandelboxes(globalCtx context.Context, globalCancel context.CancelFunc, goroutineTracker *sync.WaitGroup, dockerClient dockerclient.CommonAPIClient, mandelboxDieChan chan bool) error {
	// We don't need to start more waiting mandelboxes
	// if the global context has already been cancelled.
	select {
	case <-globalCtx.Done():
		return nil
	default:
	}

	// Skip monitoring waiting mandelboxes when running on local env.
	if metadata.IsLocalEnvWithoutDB() {
		return nil
	}

	instanceID := metadata.CloudMetadata.GetInstanceID()

	capacity, err := dbdriver.GetInstanceCapacity(string(instanceID))
	if err != nil {
		return utils.MakeError("couldn't get instance capacity: %s", err)
	}

	logger.Infof("Instance has a remaining capacity of %v, current number of waiting mandelboxes is %v", capacity, mandelboxData.GetMandelboxCount())

	newWaitingMandelboxes := int32(capacity) - mandelboxData.GetMandelboxCount()
	// If the remaining capacity field changes, check how many mandelboxes are currently
	// running and start mandelbox zygotes as necessary.
	if newWaitingMandelboxes > 0 {
		logger.Infof("Starting %v new waiting mandelboxes.", newWaitingMandelboxes)
		SpinUpMandelboxes(globalCtx, globalCancel, goroutineTracker, dockerClient, mandelboxDieChan, string(instanceID), newWaitingMandelboxes)
	}

	return nil
}

// removeStaleMandelboxesGoroutine starts
func removeStaleMandelboxesGoroutine(globalCtx context.Context) {
	defer logger.Infof("Finished removeStaleMandelboxes goroutine.")
	timerChan := make(chan interface{})

	// Instead of running exactly every 10 seconds, we choose a random time in
	// the range [9.5, 10.5] seconds to space out cleaning stale mandelboxes.
	for {
		sleepTime := 10000 - rand.Intn(1001)
		timer := time.AfterFunc(time.Duration(sleepTime)*time.Millisecond, func() { timerChan <- struct{}{} })

		select {
		case <-globalCtx.Done():
			// Remove allocated stale mandelboxes one last time
			mandelboxData.RemoveStaleMandelboxes(time.Duration(90*time.Second), time.Duration(10*time.Second))
			utils.StopAndDrainTimer(timer)
			return

		case <-timerChan:
			mandelboxData.RemoveStaleMandelboxes(time.Duration(90*time.Second), time.Duration(10*time.Second))
		}
	}
}

// ---------------------------
// Service shutdown and initialization
// ---------------------------

//go:embed mandelbox-apparmor-profile
var appArmorProfile string

// Create and register the AppArmor profile for Docker to use with
// mandelboxes. These do not persist, so must be done at service
// startup.
func initializeAppArmor() error {
	cmd := exec.Command("apparmor_parser", "-Kr")
	stdin, err := cmd.StdinPipe()
	if err != nil {
		return utils.MakeError("unable to attach to process 'stdin' pipe: %s", err)
	}

	go func() {
		defer stdin.Close()
		io.WriteString(stdin, appArmorProfile)
	}()

	err = cmd.Run()
	if err != nil {
		return utils.MakeError("unable to register AppArmor profile: %s", err)
	}

	return nil
}

// Create the directory used to store the mandelbox resource allocations
// (e.g. TTYs) on disk
func initializeFilesystem() error {
	// Check if the instance has ephemeral storage. If it does, set the WhistDir path to the
	// ephemeral volume, which should already be mounted by the userdata script. We use the
	// command below to check if the instance has an ephemeral device present.
	// See: https://stackoverflow.com/questions/10781516/how-to-pipe-several-commands-in-go
	ephemeralDeviceCmd := "nvme list -o json | jq -r '.Devices | map(select(.ModelNumber == \"Amazon EC2 NVMe Instance Storage\")) | max_by(.PhysicalSize) | .DevicePath'"
	out, err := exec.Command("bash", "-c", ephemeralDeviceCmd).CombinedOutput()
	if err != nil {
		logger.Errorf("error while getting ephemeral device path, not using ephemeral storage.")
	}

	ephemeralDevicePath := string(out)

	// We check if the command exited successfully, and if the ephemeral device exists.
	// The string will contain "bash" if something went wrong. Also take into account
	// if the WhistDir already contains the ephemeral path.
	if !metadata.IsLocalEnv() &&
		ephemeralDevicePath != "" && ephemeralDevicePath != "null" &&
		!strings.Contains(ephemeralDevicePath, "bash") && !strings.Contains(utils.WhistDir, utils.WhistEphemeralFSPath) {
		logger.Infof("Creating Whist directory on ephemeral device.")
		utils.WhistDir = path.Join(utils.WhistEphemeralFSPath, utils.WhistDir)
		utils.TempDir = path.Join(utils.WhistDir, "temp/")
	}

	// check if "/whist" already exists --- if so, panic, since
	// we don't know why it's there or if it's valid. The host-service shutting down
	// from this panic will clean up the directory and the next run will work properly.
	if _, err := os.Lstat(utils.WhistDir); !os.IsNotExist(err) {
		if err == nil {
			return utils.MakeError("directory %s already exists!", utils.WhistDir)
		} else {
			return utils.MakeError("could not make directory %s: %s", utils.WhistDir, err)
		}
	}

	// Create the whist directory and make it non-root user owned so that
	// non-root users in mandelboxes can access files within (especially user
	// configs).
	err = os.MkdirAll(utils.WhistDir, 0777)
	if err != nil {
		return utils.MakeError("failed to create directory %s: %s\n", utils.WhistDir, err)
	}
	cmd := exec.Command("chown", "-R", "ubuntu", utils.WhistDir)
	cmd.Run()

	// Create whist-private directory
	err = os.MkdirAll(utils.WhistPrivateDir, 0777)
	if err != nil {
		return utils.MakeError("failed to create directory %s: %s\n", utils.WhistPrivateDir, err)
	}

	// Create whist temp directory (only let root read and write this, since it
	// contains logs and uinput sockets).
	err = os.MkdirAll(utils.TempDir, 0600)
	if err != nil {
		return utils.MakeError("could not mkdir path %s: %s", utils.TempDir, err)
	}

	return nil
}

// Delete the directory used to store the mandelbox resource allocations (e.g.
// TTYs) on disk, as well as the directory used to store the SSL certificate we
// use for the httpserver, and our temporary directory.
func uninitializeFilesystem() {
	logger.Infof("removing all files")
	err := os.RemoveAll(utils.WhistDir)
	if err != nil {
		logger.Errorf("failed to delete directory %s: %s", utils.WhistDir, err)
		metrics.Increment("ErrorRate")
	} else {
		logger.Infof("Successfully deleted directory %s", utils.WhistDir)
	}

	err = os.RemoveAll(utils.WhistPrivateDir)
	if err != nil {
		logger.Errorf("failed to delete directory %s: %s", utils.WhistPrivateDir, err)
		metrics.Increment("ErrorRate")
	} else {
		logger.Infof("Successfully deleted directory %s", utils.WhistPrivateDir)
	}

	err = os.RemoveAll(utils.TempDir)
	if err != nil {
		logger.Errorf("failed to delete directory %s: %s", utils.TempDir, err)
		metrics.Increment("ErrorRate")
	} else {
		logger.Infof("Successfully deleted directory %s", utils.TempDir)
	}
}

func main() {
	// Set Sentry tags
	tags, err := metadata.CloudMetadata.GetMetadata()
	logger.AddSentryTags(tags)

	// Add some additional fields for Logz.io
	tags["component"] = "backend"
	tags["sub-component"] = "host-service"
	logger.AddLogzioFields(tags)

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

	// Start Docker
	dockerClient, err := createDockerClient()
	if err != nil {
		logger.Panic(globalCancel, err)
	}

	defer func() {
		// This function cleanly shuts down the Whist Host-Service. Note that
		// besides the host machine itself being forcefully shut down, this
		// deferred function from main() should be the _only_ way that the host
		// service exits. In particular, it should be as a result of a panic() in
		// main, the global context being cancelled, or a Ctrl+C interrupt.

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

		// Cancel the global context, if it hasn't already been cancelled.
		globalCancel()

		// Clean all the waiting mandelboxes so they don't block the shut down.
		// when this function exits. Stop after we cancel the global context so
		// that subscriptions are stopped and we don't trigger any database event.
		mandelboxData.StopWaitingMandelboxes(dockerClient)

		// Wait for all goroutines to stop, so we can run the rest of the cleanup
		// process.
		utils.WaitWithDebugPrints(&goroutineTracker, 2*time.Minute, 2)

		// Stop processing new events
		close(eventLoopKeepalive)

		uninitializeFilesystem()

		// Remove our row from the database and close out the database driver.
		dbdriver.Close()

		// Close out our metrics collection.
		metrics.Close()

		// Drain to our remote logging providers, but don't yet stop recording new
		// events, in case the shutdown fails.
		logger.Sync()

		logger.Info("Finished host service shutdown procedure. Finally exiting...")
		if shutdownInstanceOnExit {
			if err := exec.Command("shutdown", "now").Run(); err != nil {
				if strings.TrimSpace(err.Error()) == "signal: terminated" {
					// The instance seems to shut down even when this error is fired, so
					// we'll just ignore it. We still `logger.Info()` it just in case.
					logger.Infof("Shutdown command returned 'signal: terminated' error. Ignoring it.")
				} else {
					logger.Errorf("couldn't shut down instance: %s", err)
					metrics.Increment("ErrorRate")
				}
			}
		}

		os.Exit(0)
	}()

	// Log the Git commit of the running executable
	logger.Infof("Host Service Version: %s", metadata.GetGitCommit())

	var hostStartupErr *multierror.Error
	// Initialize the database driver, if necessary (the `dbdriver` package
	// takes care of the "if necessary" part).
	if err := dbdriver.Initialize(globalCtx, &goroutineTracker); err != nil {
		hostStartupErr = multierror.Append(hostStartupErr, err)
		globalCancel()
	}

	// Log the instance name we're running on
	instanceName := metadata.CloudMetadata.GetInstanceName()
	logger.Infof("Running on instance name: %s", instanceName)

	if err = initializeAppArmor(); err != nil {
		hostStartupErr = multierror.Append(hostStartupErr, err)
		globalCancel()
	}

	if err = initializeFilesystem(); err != nil {
		hostStartupErr = multierror.Append(hostStartupErr, err)
		globalCancel()
	}

	if err := dbdriver.RegisterInstance(); err != nil {
		// If the instance starts up and sees its status as unresponsive or
		// draining, the backend doesn't want it anymore so we should shut down.

		// TODO: make this a bit more robust
		if !metadata.IsLocalEnv() && strings.Contains(err.Error(), string(dbdriver.InstanceStatusDraining)) {
			logger.Infof("Instance wasn't registered in database because we found ourselves already marked draining. Shutting down.... Error: %s", err)
			shutdownInstanceOnExit = true
			globalCancel()
		} else {
			hostStartupErr = multierror.Append(hostStartupErr, err)
			globalCancel()
		}
	}

	// Start database subscription client
	instanceID := metadata.CloudMetadata.GetInstanceID()

	capacity, err := dbdriver.GetInstanceCapacity(string(instanceID))
	if err != nil {
		logger.Errorf("failed to get capacity of instance %s: %s", instanceID, err)
	}

	// Now we start all the goroutines that actually do work.

	// Start a goroutine that removes stale mandelboxes.
	goroutineTracker.Add(1)
	go func() {
		defer goroutineTracker.Done()
		removeStaleMandelboxesGoroutine(globalCtx)
	}()

	// Start the HTTP server and listen for events
	httpServerEvents, err := StartHTTPServer(globalCtx, globalCancel, &goroutineTracker)
	if err != nil {
		hostStartupErr = multierror.Append(hostStartupErr, err)
		globalCancel()
	}

	if hostStartupErr != nil {
		logger.Panicf(globalCancel, utils.Sprintf("host service startup, %s", hostStartupErr))
	}

	subscriptionEvents := make(chan subscriptions.SubscriptionEvent, 100)
	// It's not necessary to subscribe to the config database
	// in the host service
	useConfigDB := false

	subscriptionClient := &subscriptions.SubscriptionClient{}
	subscriptions.SetupHostSubscriptions(string(instanceID), subscriptionClient)
	subscriptions.Start(subscriptionClient, globalCtx, &goroutineTracker, subscriptionEvents, useConfigDB)
	if err != nil {
		logger.Errorf("failed to start database subscriptions: %s", err)
	}

	mandelboxDieEvents := make(chan bool, 10)

	// Start warming up as many instances as we have capacity for. This will effectively create
	// mandelboxes up to the point where we need a config token, and register them to the database.
	// The scaling service will handling assigning users to this instance and will update the
	// database row to assign the user to a waiting mandelbox.
	SpinUpMandelboxes(globalCtx, globalCancel, &goroutineTracker, dockerClient, mandelboxDieEvents, string(instanceID), capacity)

	// Start main event loop. Note that we don't track this goroutine, but
	// instead control its lifetime with `eventLoopKeepAlive`. This is because it
	// needs to stay alive after the global context is cancelled, so we can
	// process mandelbox death events.
	go eventLoopGoroutine(globalCtx, globalCancel, &goroutineTracker, dockerClient, httpServerEvents, subscriptionEvents, mandelboxDieEvents)

	// Register a signal handler for Ctrl-C so that we cleanup if Ctrl-C is pressed.
	sigChan := make(chan os.Signal, 2)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM, syscall.SIGINT)

	// Wait for either the global context to get cancelled by a worker goroutine,
	// or for us to receive an interrupt. This needs to be the end of main().
	select {
	case <-sigChan:
		logger.Infof("Got an interrupt or SIGTERM")
	case <-globalCtx.Done():
		logger.Infof("Global context cancelled!")
	}
}

// As long as this channel is blocking, we continue processing events
// (including Docker events).
var eventLoopKeepalive = make(chan interface{}, 1)

func eventLoopGoroutine(globalCtx context.Context, globalCancel context.CancelFunc,
	goroutineTracker *sync.WaitGroup, dockerClient dockerclient.CommonAPIClient,
	httpServerEvents <-chan httputils.ServerRequest, subscriptionEvents <-chan subscriptions.SubscriptionEvent, mandelboxDieEvents chan bool) {
	// Note that we don't use globalCtx for the Docker Context, since we still
	// wish to process Docker events after the global context is cancelled.
	dockerContext, dockerContextCancel := context.WithCancel(context.Background())
	defer dockerContextCancel()

	// Create filter for which docker events we care about
	filters := dockerfilters.NewArgs()
	filters.Add("type", dockerevents.ContainerEventType)
	eventOptions := dockertypes.EventsOptions{
		Filters: filters,
	}

	// We use this lock to protect the transportRequestMap
	transportMapLock := &sync.Mutex{}

	// Note: If a mandelbox suffers a bug, or fails to start correctly
	// these channels will become a memory leak.
	transportRequestMap := make(map[mandelboxtypes.MandelboxID]chan *httputils.JSONTransportRequest)

	// In the following loop, this var determines whether to re-initialize the
	// Docker event stream. This is necessary because the Docker event stream
	// needs to be reopened after any error is sent over the error channel.
	needToReinitDockerEventStream := false
	dockerevents, dockererrs := dockerClient.Events(dockerContext, eventOptions)
	logger.Info("Initialized docker event stream.")

	logger.Info("Entering event loop...")

	// The actual event loop
	for {
		if needToReinitDockerEventStream {
			dockerevents, dockererrs = dockerClient.Events(dockerContext, eventOptions)
			needToReinitDockerEventStream = false
			logger.Info("Re-initialized docker event stream.")
		}

		select {
		case <-eventLoopKeepalive:
			logger.Infof("Leaving main event loop...")
			return

		case err := <-dockererrs:
			needToReinitDockerEventStream = true
			switch {
			case err == nil:
				logger.Info("We got a nil error over the Docker event stream. This might indicate an error inside the docker go library. Ignoring it and proceeding normally...")
				continue
			case err == io.EOF:
				logger.Panicf(globalCancel, "docker event stream has been completely read.")
			case dockerclient.IsErrConnectionFailed(err):
				// This means "Cannot connect to the Docker daemon..."
				logger.Panicf(globalCancel, "could not connect to the Docker daemon: %s", err)
			default:
				if !strings.HasSuffix(strings.ToLower(err.Error()), "context canceled") {
					logger.Panicf(globalCancel, "got an unknown error from the Docker event stream: %s", err)
				}
				return
			}

		case dockerevent := <-dockerevents:
			logger.Infof("dockerevent: %s for %s %s\n", dockerevent.Action, dockerevent.Type, dockerevent.ID)
			if dockerevent.Action == "die" {
				mandelboxDieHandler(dockerevent.ID, transportRequestMap, transportMapLock, dockerClient)
			}
		case <-mandelboxDieEvents:
			monitorWaitingMandelboxes(globalCtx, globalCancel, goroutineTracker, dockerClient, mandelboxDieEvents)

		// It may seem silly to just launch goroutines to handle these
		// serverevents, but we aim to keep the high-level flow control and handling
		// in this package, and the low-level authentication, parsing, etc. of
		// requests in `httpserver`.
		case serverevent := <-httpServerEvents:
			switch serverevent := serverevent.(type) {
			// TODO: actually handle panics in these goroutines
			case *httputils.JSONTransportRequest:
				if !metadata.IsLocalEnvWithoutDB() {
					// Handle JSON transport validation on a separate goroutine
					go handleJSONTransportRequest(serverevent, transportRequestMap, transportMapLock)
				} else {
					// If running on a local environment, disable any pubsub logic. We have to create a subscription request
					// that mocks the Hasura subscription event. Doing this avoids the need of setting up a Hasura server and
					// postgres database on the development instance.
					jsonReq := serverevent

					userID := metadata.CloudMetadata.GetUserID()
					instanceID := metadata.CloudMetadata.GetInstanceID()

					// Create a mandelbox object as would be received from a Hasura subscription.
					mandelbox := subscriptions.Mandelbox{
						InstanceID: string(instanceID),
						ID:         jsonReq.MandelboxID,
						SessionID:  utils.Sprintf("%v", time.Now().UnixMilli()),
						UserID:     userID,
					}
					subscriptionEvent := subscriptions.MandelboxEvent{
						Mandelboxes: []subscriptions.Mandelbox{mandelbox},
					}
					// mandelboxSubscription is the pubsub event received from Hasura.
					mandelboxSubscription := subscriptionEvent.Mandelboxes[0]

					// Launch the JSON transport handler to be able to call getAppName and obtain appName and req, needed to spin up the mandelbox.
					go handleJSONTransportRequest(serverevent, transportRequestMap, transportMapLock)
					req, appName := getAppName(mandelboxSubscription, transportRequestMap, transportMapLock)

					// For local development, we start and finish the mandelbox spin up back to back
					zygote, err := StartMandelboxSpinUp(globalCtx, globalCancel, goroutineTracker, dockerClient, jsonReq.MandelboxID, appName, mandelboxDieEvents)
					if err != nil {
						logger.Errorw(utils.Sprintf("failed to start waiting mandelbox: %s", err), []interface{}{
							zap.String("mandelbox_id", zygote.GetID().String()),
							zap.Time("updated_at", zygote.GetLastUpdatedTime()),
						})
						return
					}

					go func() {
						err := FinishMandelboxSpinUp(globalCtx, globalCancel, goroutineTracker, dockerClient, mandelboxSubscription, transportRequestMap, transportMapLock, req)
						if err != nil {
							logger.Errorw(utils.Sprintf("failed to finish mandelbox startup: %s", err), []interface{}{
								zap.String("instance_id", mandelboxSubscription.InstanceID),
								zap.String("mandelbox_id", mandelboxSubscription.ID.String()),
								zap.String("mandelbox_app", mandelboxSubscription.App),
								zap.String("session_id", mandelboxSubscription.SessionID),
								zap.String("user_id", string(mandelboxSubscription.UserID)),
								zap.Time("created_at", mandelboxSubscription.CreatedAt),
								zap.Time("updated_at", mandelboxSubscription.UpdatedAt),
							})
						}
					}()
				}
			default:
				if serverevent != nil {
					err := utils.MakeError("unimplemented handling of server event [type: %T]: %v", serverevent, serverevent)
					logger.Error(err)
					serverevent.ReturnResult("", err)
				}
			}

		case subscriptionEvent := <-subscriptionEvents:
			switch subscriptionEvent := subscriptionEvent.(type) {
			case *subscriptions.MandelboxEvent:
				mandelboxSubscription := subscriptionEvent.Mandelboxes[0]
				req, _ := getAppName(mandelboxSubscription, transportRequestMap, transportMapLock)
				go func() {
					err := FinishMandelboxSpinUp(globalCtx, globalCancel, goroutineTracker, dockerClient,
						mandelboxSubscription, transportRequestMap, transportMapLock, req)
					if err != nil {
						logger.Errorw(utils.Sprintf("failed to finish mandelbox startup: %s", err), []interface{}{
							zap.String("instance_id", mandelboxSubscription.InstanceID),
							zap.String("mandelbox_id", mandelboxSubscription.ID.String()),
							zap.String("mandelbox_app", mandelboxSubscription.App),
							zap.String("session_id", mandelboxSubscription.SessionID),
							zap.String("user_id", string(mandelboxSubscription.UserID)),
							zap.Time("created_at", mandelboxSubscription.CreatedAt),
							zap.Time("updated_at", mandelboxSubscription.UpdatedAt),
						})
					}
				}()

			case *subscriptions.InstanceEvent:
				if len(subscriptionEvent.Instances) == 0 {
					break
				}
				instance := subscriptionEvent.Instances[0]

				// If the status of the instance changes to "DRAINING", cancel the global context and exit.
				if instance.Status == string(dbdriver.InstanceStatusDraining) {
					// Don't do this in a separate goroutine, since there's no reason to.
					drainAndShutdown(globalCtx, globalCancel, goroutineTracker)
					break
				}

			default:
				if subscriptionEvent != nil {
					err := utils.MakeError("unimplemented handling of subscription event [type: %T]: %v", subscriptionEvent, subscriptionEvent)
					logger.Error(err)
				}
			}
		}
	}
}
