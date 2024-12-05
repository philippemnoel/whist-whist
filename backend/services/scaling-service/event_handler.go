// Copyright (c) 2021-2022 Whist Technologies, Inc.

/*
The scaling service is a server responsible of managing the state of the backend.
It manages the number of instances running by starting and terminating according
to demand. Additionally, it assigns users to available instances, and registers a
mandelbox on the database so the host service running on the instance can start it.
This component is also responsible of validating payments, as well as interacting
with Stripe to create new subscriptions.

The scaling service is an event-driven component, so most of its logic is handled using
go channels, in reaction to external events. It has many layers, the bottom layer being
the event handler which only passes the events to the corresponding channels. The layer
on top of it includes the many scaling algorithms which are sub-processes for each region
on the backend. The code is written in an extensible way so it is possible to write and
integrate a new scaling algorithm with a different behaviour easily. As such, most of
the code is inside the scaling algorithm implementations.

The top-most layer has the "hosts handlers" which are a collection of methods responsible
of calling a cloud provider (e.g. AWS, Azure, Google Cloud) API and starting an instance
(cloud-compute resource configured to run a Whist container). This code is abstracted in
an interface so that support for a new cloud provider is easily added. The code in the
algorithms is provider-agnostic, tied only to the necessities of the backend.
*/

package main

import (
	"context"
	"encoding/json"
	"flag"
	"os"
	"os/signal"
	"strings"
	"sync"
	"syscall"
	"time"

	"path"

	"github.com/go-co-op/gocron"
	"github.com/google/uuid"
	"github.com/whisthq/whist/backend/services/metadata"
	"github.com/whisthq/whist/backend/services/scaling-service/config"
	"github.com/whisthq/whist/backend/services/scaling-service/dbclient"
	hosts "github.com/whisthq/whist/backend/services/scaling-service/hosts/aws"
	algos "github.com/whisthq/whist/backend/services/scaling-service/scaling_algorithms/default" // Import as algos, short for scaling_algorithms
	"github.com/whisthq/whist/backend/services/subscriptions"
	"github.com/whisthq/whist/backend/services/utils"
	logger "github.com/whisthq/whist/backend/services/whistlogger"
)

func main() {
	var (
		cleanupPeriod time.Duration
		noCleanup bool
	)

	flag.DurationVar(&cleanupPeriod, "cleanup", time.Duration(time.Minute),
		"the amount of time between when each cleanup thread runs")
	flag.BoolVar(&noCleanup, "nocleanup", false, "disable asynchronous cleanup "+
		"of unresponsive instances")
	flag.Parse()

	globalCtx, globalCancel := context.WithCancel(context.Background())
	goroutineTracker := &sync.WaitGroup{}

	// Add some additional fields for Logz.io
	tags := make(map[string]string)
	tags["component"] = "backend"
	tags["sub-component"] = "scaling-service"
	logger.AddLogzioFields(tags)

	var (
		dbClient            dbclient.WhistDBClient                // The client that abstracts database interactions
		graphqlClient       subscriptions.WhistGraphQLClient      // The GraphQL client to query the Hasura server
		configGraphqlClient subscriptions.WhistGraphQLClient      // The GraphQL client to query the config Hasura server
		subscriptionClient  subscriptions.WhistSubscriptionClient // The subscription client to subscribe to the Hasura server
		configClient        subscriptions.WhistSubscriptionClient // The subscriptions client to subscribe to the config Hasura server
		subscriptionEvents  chan subscriptions.SubscriptionEvent  // Channel to process database subscription events
		scheduledEvents     chan algos.ScalingEvent               // Channel to process scheduled events

	)

	// Start HTTP Server for assigning mandelboxes
	serverEvents := make(chan algos.ScalingEvent, 100)
	StartHTTPServer(serverEvents)

	// Start GraphQL client for queries/mutations
	useConfigDB := false
	graphqlClient = &subscriptions.GraphQLClient{}
	err := graphqlClient.Initialize(useConfigDB)
	if err != nil {
		logger.Errorf("failed to start GraphQL client: %s", err)
	}

	// Start GraphQL client for getting configuration from the config db
	useConfigDB = true
	configGraphqlClient = &subscriptions.GraphQLClient{}
	err = configGraphqlClient.Initialize(useConfigDB)
	if err != nil {
		logger.Errorf("failed to start config GraphQL client: %s", err)
	}

	if err := config.Initialize(globalCtx, configGraphqlClient); err != nil {
		logger.Errorf("Failed to retrieve configuration values: %s", err)
		return
	}

	// Start database subscriptions
	subscriptionEvents = make(chan subscriptions.SubscriptionEvent, 100)
	subscriptionClient = &subscriptions.SubscriptionClient{}
	configClient = &subscriptions.SubscriptionClient{}
	StartDatabaseSubscriptions(globalCtx, goroutineTracker, subscriptionEvents, subscriptionClient, configClient)

	// Initialize database client
	dbClient = &dbclient.DBClient{}

	// Start scheduler and setup scheduler event chan
	scheduledEvents = make(chan algos.ScalingEvent, 100)

	// Set to run every 10 minutes, starting 10 minutes from now
	start := time.Duration(10 * time.Minute)
	StartSchedulerEvents(scheduledEvents, 10, start)

	// Start the deploy events once since we are starting the scaling service.
	StartDeploy(scheduledEvents)

	// algorithmByRegionMap holds all of the scaling algorithms mapped by region.
	// Use a sync map since we only write the keys once but will be reading multiple
	// times by different goroutines.
	algorithmByRegionMap := &sync.Map{}
	regions := config.GetEnabledRegions()
	stopFuncs := make([]func(), 0, len(regions))

	// Load and instantiate default scaling algorithm for all enabled regions.
	for _, region := range regions {
		name := utils.Sprintf("default-sa-%s", region)
		handler := &hosts.AWSHost{}

		if err := handler.Initialize(region); err != nil {
			logger.Errorf("Failed to initialize host handler for region '%s'", region)
			continue
		}

		algo := &algos.DefaultScalingAlgorithm{Host: handler, Region: region}

		algo.CreateEventChans()
		algo.CreateGraphQLClient(graphqlClient)
		algo.CreateDBClient(dbClient)
		algo.ProcessEvents(globalCtx, goroutineTracker)

		if noCleanup {
			logger.Infof("Cleanup disabled. Not starting cleanup threads.")
		} else {
			stop := CleanRegion(graphqlClient, handler, cleanupPeriod)
			stopFuncs = append(stopFuncs, stop)
			logger.Infof("Unresponsive instances will be pruned every %s.",
				cleanupPeriod)
		}

		algorithmByRegionMap.Store(name, algo)

		logger.Infof("There should be as close as possible to %d unassigned "+
			"Mandelboxes available at all times in %s",
			config.GetTargetFreeMandelboxes(region), region)
	}

	// Wait for each of our cleanup threads to finish before we exit.
	defer func() {
		var wg sync.WaitGroup

		for j := range stopFuncs {
			wg.Add(1)

			go func(i int) {
				defer wg.Done()
				stopFuncs[i]()
			}(j)
		}

		wg.Wait()
	}()

	// Start main event loop
	go eventLoop(globalCtx, globalCancel, serverEvents, subscriptionEvents, scheduledEvents, algorithmByRegionMap, configClient)

	// Register a signal handler for Ctrl-C so that we cleanup if Ctrl-C is pressed.
	sigChan := make(chan os.Signal, 2)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM, syscall.SIGINT)

	// Wait for either the global context to get cancelled by a worker goroutine,
	// or for us to receive an interrupt. This needs to be the end of main().
	select {
	case <-sigChan:
		logger.Infof("Got an interrupt or SIGTERM")
		logger.Sync()
	case <-globalCtx.Done():
		logger.Infof("Global context cancelled!")
		logger.Sync()
	}
}

// StartDatabaseSubscriptions sets up the database subscriptions and starts the subscription client.
func StartDatabaseSubscriptions(globalCtx context.Context, goroutineTracker *sync.WaitGroup, subscriptionEvents chan subscriptions.SubscriptionEvent, subscriptionClient subscriptions.WhistSubscriptionClient, configClient subscriptions.WhistSubscriptionClient) {
	// Setup and start subscriptions to main database

	// The first client will subscribe to the dev/staging/prod database
	useConfigDatabase := false

	subscriptions.SetupScalingSubscriptions(subscriptionClient)
	err := subscriptions.Start(subscriptionClient, globalCtx, goroutineTracker, subscriptionEvents, useConfigDatabase)
	if err != nil {
		logger.Errorf("failed to start database subscription client: %s", err)
	}

	// The second client will subscribe to the config database
	useConfigDatabase = true

	// Setup and start subscriptions to config database
	subscriptions.SetupConfigSubscriptions(configClient)
	err = subscriptions.Start(configClient, globalCtx, goroutineTracker, subscriptionEvents, useConfigDatabase)
	if err != nil {
		logger.Errorf("failed to start config database subscription client: %s", err)
	}
}

// StartSchedulerEvents starts the scheduler and its events without blocking the main thread.
// `interval` sets the time when the event will run in minutes (i.e. every 10 minutes), and `start`
// sets the time when the first event will happen (i.e. 10 minutes from now).
func StartSchedulerEvents(scheduledEvents chan algos.ScalingEvent, interval interface{}, start time.Duration) {
	if metadata.IsLocalEnvWithoutDB() && !metadata.IsRunningInCI() {
		return
	}

	s := gocron.NewScheduler(time.UTC)

	// Schedule scale down routine every 10 minutes, start 10 minutes from now.
	t := time.Now().Add(start)
	s.Every(interval).Minutes().StartAt(t).Do(func() {
		// Send into scheduling channel
		scheduledEvents <- algos.ScalingEvent{
			// Create a UUID so we can identify and search this event on our logs
			ID: uuid.NewString(),
			// We set the event type to SCHEDULED_SCALE_DOWN_EVENT
			// here so that we have more information about the event.
			// SCHEDULED means the source of the event is the scheduler
			// and SCALE_DOWN means it will perform a scale down action.
			Type: "SCHEDULED_SCALE_DOWN_EVENT",
		}
	})

	s.StartAsync()
}

// StartDeploy reads the `images.json` file which is written by the Github
// deploy workflow, and sends the event to the appropiate channel.
func StartDeploy(scheduledEvents chan algos.ScalingEvent) {
	if metadata.IsLocalEnv() && !metadata.IsRunningInCI() {
		logger.Infof("Running on localdev so not performing deploy actions.")
		return
	}

	regionImageMap, err := getRegionImageMap()
	if err != nil {
		logger.Error(err)
		return
	}

	// Send image upgrade event to scheduled chan.
	scheduledEvents <- algos.ScalingEvent{
		// Create a UUID so we can identify and search this event on our logs
		ID: uuid.NewString(),
		// We set the event type to SCHEDULED_IMAGE_UPGRADE_EVENT
		// here so that we have more information about the event.
		// SCHEDULED means the source of the event is the scheduler
		// and IMAGE_UPGRADE means it will perform an image upgrade action.
		Type: "SCHEDULED_IMAGE_UPGRADE_EVENT",
		Data: regionImageMap,
	}
}

// getRegionImageMap tries to read and parse the contents of the `images.json` file
// which is written by the Github deploy workflow.
func getRegionImageMap() (map[string]interface{}, error) {
	var (
		regionImageMap map[string]interface{}
		filename       string
	)

	// Read file which contains the region to image on JSON format. This file will
	// be read by the binary generated during deploy, located in the `bin` directory.
	// The file is written to /etc/whist/images.json when building the Docker container.
	if metadata.IsLocalEnv() {
		// Get current working directory to read images file.
		currentWorkingDirectory, err := os.Getwd()
		if err != nil {
			return nil, utils.MakeError("failed to get working directory: %s", err)
		}
		filename = path.Join(currentWorkingDirectory, "images.json")
	} else {
		filename = "/etc/whist/images.json"
	}
	content, err := os.ReadFile(filename)
	if err != nil {
		return nil, utils.MakeError("failed to read region to image map from file: %s", err)
	}

	// We have to remove the back slashes from the escaped json string
	// before unmarshalling.
	jsonString := strings.ReplaceAll(string(content), `\`, "")

	// Try to unmarshal contents of file into a map
	err = json.Unmarshal([]byte(jsonString), &regionImageMap)
	if err != nil {
		return nil, utils.MakeError("failed to unmarshal region to image map: %s", err)
	}

	return regionImageMap, nil
}

// getScalingAlgorithm is a helper function that returns the scaling algorithm from the sync map.
func getScalingAlgorithm(algorithmByRegion *sync.Map, scalingEvent algos.ScalingEvent) algos.ScalingAlgorithm {
	// Try to get the scaling algorithm on the region the scaling event was requested.
	// If no region is specified, use the default region.
	// TODO: figure out how to get non-default scaling algorihtms.
	var (
		name   string
		region string
	)
	const defaultRegion = "us-east-1"

	region = scalingEvent.Region

	if region == "" {
		logger.Infof("No region found in scaling event, using default region %s", defaultRegion)
		name = utils.Sprintf("default-sa-%s", defaultRegion)
	} else {
		name = utils.Sprintf("default-sa-%s", scalingEvent.Region)
	}

	algorithm, ok := algorithmByRegion.Load(name)
	if ok {
		return algorithm.(algos.ScalingAlgorithm)
	}

	logger.Warningf("Failed to get scaling algorithm in %s", scalingEvent.Region)

	return nil
}

// eventLoop is the main loop of the scaling service which will receive events from different sources
// and send them to the appropiate channels.
func eventLoop(globalCtx context.Context, globalCancel context.CancelFunc, serverEvents <-chan algos.ScalingEvent, subscriptionEvents <-chan subscriptions.SubscriptionEvent,
	scheduledEvents <-chan algos.ScalingEvent, algorithmByRegion *sync.Map, configClient subscriptions.WhistSubscriptionClient) {

	for {
		select {
		case subscriptionEvent := <-subscriptionEvents:
			// Since we are dealing with a database event, figure out what type it has
			// and send it to the corresponding channel
			switch subscriptionEvent := subscriptionEvent.(type) {

			case *subscriptions.InstanceEvent:
				var scalingEvent algos.ScalingEvent

				// We set the event type to DATABASE_INSTANCE_EVENT
				// here so that we have more information about the event.
				// DATABASE means the source of the event is the database
				// and INSTANCE_EVENT means it will perform an action with
				// the instance information received from the database.
				scalingEvent.Type = "DATABASE_INSTANCE_EVENT"

				if len(subscriptionEvent.Instances) > 0 {
					instance := subscriptionEvent.Instances[0]
					scalingEvent.ID = uuid.NewString()
					scalingEvent.Data = instance
					scalingEvent.Region = instance.Region
				}

				// Start scaling algorithm based on region
				algorithm := getScalingAlgorithm(algorithmByRegion, scalingEvent)

				switch algorithm := algorithm.(type) {
				case *algos.DefaultScalingAlgorithm:
					algorithm.InstanceEventChan <- scalingEvent
				}
			case *subscriptions.FrontendVersionEvent:
				var (
					scalingEvent algos.ScalingEvent
					version      subscriptions.FrontendVersion
				)

				// We set the event type to DATABASE_FRONTEND_VERSION_EVENT
				// here so that we have more information about the event.
				// DATABASE means the source of the event is the database
				// and FRONTEND_VERSION means it will perform an action with
				// the frontend version information received from the database.
				scalingEvent.Type = "DATABASE_FRONTEND_VERSION_EVENT"

				// We only expect one version to come from the database subscription,
				// anything more or less than it indicates an error in our backend.
				if len(subscriptionEvent.FrontendVersions) == 1 {
					version = subscriptionEvent.FrontendVersions[0]
				} else {
					logger.Errorf("unexpected length of %d in version received from the config database", len(subscriptionEvent.FrontendVersions))
				}

				regionImageMap, err := getRegionImageMap()
				if err != nil {
					logger.Error(err)
					break
				}

				for region := range regionImageMap {
					scalingEvent.Region = region
					scalingEvent.Data = version

					// Start scaling algorithm based on region
					algorithm := getScalingAlgorithm(algorithmByRegion, scalingEvent)

					switch algorithm := algorithm.(type) {
					case *algos.DefaultScalingAlgorithm:
						algorithm.FrontendVersionChan <- scalingEvent
					}
				}

				// Its necessary to close the config client since we only want to
				// do the image swapover once, but Hasura will keep receiving
				// the event after it has switched.
				err = configClient.Close()
				if err != nil {
					// err is already wrapped here
					logger.Error(err)
				}
			}

		case scheduledEvent := <-scheduledEvents:
			// Start scaling algorithm based on region
			for _, region := range config.GetEnabledRegions() {
				scheduledEvent.Region = region
				algorithm := getScalingAlgorithm(algorithmByRegion, scheduledEvent)
				switch algorithm := algorithm.(type) {
				case *algos.DefaultScalingAlgorithm:
					algorithm.ScheduledEventChan <- scheduledEvent
				}
			}
		case serverEvent := <-serverEvents:
			algorithm := getScalingAlgorithm(algorithmByRegion, serverEvent)
			switch algorithm := algorithm.(type) {
			case *algos.DefaultScalingAlgorithm:
				algorithm.ServerEventChan <- serverEvent
			}

		case <-globalCtx.Done():
			logger.Infof("Global context has been cancelled, exiting event loop...")
			return
		}
	}
}
