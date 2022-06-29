// Copyright (c) 2021-2022 Whist Technologies, Inc.

/*
Package scaling_algorithms includes the implementations of all the scaling algorithms used by
the scaling service.

A scaling algorithm is defined as struct that implements the `ScalingAlgorithm` interface
and has a collection of methods that make decisions about how and when to scale instances
in order satisfy demand. Alternative implementations or use cases for a scaling algorithm
could be: a region that has different characteristics such as less instance availability
or user demand, deploying a smarter algorithm on a region with high demand, using different
strategies for detecting user demand, or in general any situation that requires a different
behaviour than the one in the default scaling algorithm.

The currently implemented algorithms are:
- Default scaling algorithm:
	It handles the scaling of instances by computing mandelbox capacity for each instance
	and determining the overall availability on each region according to a "buffer" set by
	the config database. It applies the same scaling rules to all regions across the world.
*/

package scaling_algorithms

import (
	"context"
	"strconv"
	"strings"
	"sync"

	"github.com/whisthq/whist/backend/services/metadata"
	"github.com/whisthq/whist/backend/services/scaling-service/dbclient"
	"github.com/whisthq/whist/backend/services/scaling-service/hosts"
	aws "github.com/whisthq/whist/backend/services/scaling-service/hosts/aws"
	"github.com/whisthq/whist/backend/services/subscriptions"
	"github.com/whisthq/whist/backend/services/utils"
	logger "github.com/whisthq/whist/backend/services/whistlogger"
)

// ScalingAlgorithm is the basic abstraction of a scaling algorithm.
// It doesn't make any assumptions about the underlying implementation,
// it only defines the basic access methods that the event handler needs.
type ScalingAlgorithm interface {
	ProcessEvents(context.Context, *sync.WaitGroup)
	CreateEventChans()
	CreateGraphQLClient(subscriptions.WhistGraphQLClient)
	CreateDBClient(dbclient.WhistDBClient)
	GetConfig(subscriptions.WhistGraphQLClient)
}

// ScalingEvent is an event that contains all the relevant information
// to make scaling decisions. Its the common object used throughout the
// scaling service and passed across the different layers.
type ScalingEvent struct {
	// A unique identifier for each scaling event.
	ID string
	// The source where the event originated (database, timing, etc.)
	Type interface{}
	// Data relevant to the event.
	Data interface{}
	// Availability region where the scaling will be performed.
	Region string
}

// DefaultScalingAlgorithm abstracts the shared functionalities to be used
// by all of the different, region-based scaling algorithms.
type DefaultScalingAlgorithm struct {
	Host                   hosts.HostHandler
	GraphQLClient          subscriptions.WhistGraphQLClient
	DBClient               dbclient.WhistDBClient
	Region                 string
	InstanceEventChan      chan ScalingEvent
	ImageEventChan         chan ScalingEvent
	FrontendVersionChan    chan ScalingEvent
	ScheduledEventChan     chan ScalingEvent
	ServerEventChan        chan ScalingEvent
	SyncChan               chan bool                      // This channel is used to sync actions
	protectedFromScaleDown map[string]subscriptions.Image // Use a map to keep track of images that should not be scaled down
	protectedMapLock       sync.Mutex
}

// CreateEventChans creates the event channels if they don't alredy exist.
func (s *DefaultScalingAlgorithm) CreateEventChans() {
	// TODO: Only use one chan for database events and
	// one for scheduled events
	if s.InstanceEventChan == nil {
		s.InstanceEventChan = make(chan ScalingEvent, 100)
	}
	if s.ImageEventChan == nil {
		s.ImageEventChan = make(chan ScalingEvent, 100)
	}
	if s.FrontendVersionChan == nil {
		s.FrontendVersionChan = make(chan ScalingEvent, 100)
	}
	if s.ScheduledEventChan == nil {
		s.ScheduledEventChan = make(chan ScalingEvent, 100)
	}
	if s.ServerEventChan == nil {
		s.ServerEventChan = make(chan ScalingEvent, 100)
	}
	if s.SyncChan == nil {
		s.SyncChan = make(chan bool)
	}
}

// CreateGraphQLClient sets the graphqlClient to be used by the DBClient for queries and mutations.
func (s *DefaultScalingAlgorithm) CreateGraphQLClient(client subscriptions.WhistGraphQLClient) {
	if s.GraphQLClient == nil {
		s.GraphQLClient = client
	}
}

// CreateDBClient sets the DBClient to be used when interacting with the database.
func (s *DefaultScalingAlgorithm) CreateDBClient(dbClient dbclient.WhistDBClient) {
	if s.DBClient == nil {
		s.DBClient = dbClient
	}
}

// GetConfig will query the configuration database and populate the configuration variables
// according to the environment the scaling service is running in. It's necessary to perform
// the query before starting to receive any scaling events.
func (s *DefaultScalingAlgorithm) GetConfig(client subscriptions.WhistGraphQLClient) {
	// If on local env, use default configurations
	if metadata.IsLocalEnvWithoutDB() {
		logger.Infof("Running on localdev, using default scaling algorithm configurations...")
		return
	}

	logger.Infof("Populating config variables from config database...")
	ctx, cancel := context.WithCancel(context.TODO())
	defer cancel()

	// Get the most recent frontend version from the config database
	version, err := dbclient.GetFrontendVersion(ctx, client)
	if err != nil {
		logger.Errorf("Failed to query frontend version. Err: %v", err)
	}

	if version == (subscriptions.FrontendVersion{}) {
		logger.Errorf("Got an empty frontend version")
	}

	// Set the scaling algorithm's internal version to the one received
	// from the config database. The scaling service uses this value for
	// checking if the incoming requests are coming from an outdated frontend.
	setFrontendVersion(version)

	var configs map[string]string

	switch metadata.GetAppEnvironmentLowercase() {
	case string(metadata.EnvDev):
		configs, err = dbclient.GetDevConfigs(ctx, client)
	case string(metadata.EnvStaging):
		configs, err = dbclient.GetStagingConfigs(ctx, client)
	case string(metadata.EnvProd):
		configs, err = dbclient.GetProdConfigs(ctx, client)
	default:
		configs, err = dbclient.GetDevConfigs(ctx, client)
	}

	if err != nil {
		// Err is already wrapped here
		logger.Error(err)

		// Something went wrong, use default configuration values
		return
	}

	// Temporary map to hold all parsed values
	mandelboxRegionMap := make(map[string]int)

	// Look for each region's entry in the config database values
	// to populate the `desiredFreeMandelboxesPerRegion` map.
	for _, region := range GetEnabledRegions() {
		// Parse the region string to the format used by
		// the config database so we can get the value.
		key := strings.ReplaceAll(region, "-", "_")
		key = strings.ToUpper(key)

		configMandelboxes, ok := configs[utils.Sprintf("DESIRED_FREE_MANDELBOXES_%s", key)]
		if !ok {
			logger.Errorf("Desired mandelboxes for region %v not found on %v config database. Using default value.", region, metadata.GetAppEnvironmentLowercase())
			// Use default value of 2 if the entry for the specific
			// region was not found
			mandelboxRegionMap[region] = 2
			continue
		}

		mandelboxInt, err := strconv.Atoi(configMandelboxes)
		if err != nil {
			logger.Errorf("Error parsing desired mandelboxes value. Using default value. Err: %v", err)
			// Use default value of 2 if we failed to convert to int
			mandelboxRegionMap[region] = 2
			continue
		}

		mandelboxRegionMap[region] = mandelboxInt
	}

	desiredFreeMandelboxesPerRegion = mandelboxRegionMap

}

// ProcessEvents is the main function of the scaling algorithm, it is responsible of processing
// events and executing the appropiate scaling actions. This function is specific for each region
// scaling algorithm to be able to implement different strategies on each region.
func (s *DefaultScalingAlgorithm) ProcessEvents(globalCtx context.Context, goroutineTracker *sync.WaitGroup) {
	if s.Host == nil {
		// TODO when multi-cloud support is introduced, figure out a way to
		// decide which host to use. For now default to AWS.
		handler := &aws.AWSHost{}
		err := handler.Initialize(s.Region)

		if err != nil {
			logger.Errorf("Error starting host on region: %v. Error: %v", err, s.Region)
		}

		s.Host = handler
	}

	// Start algorithm main event loop
	// Track this goroutine so we can wait for it to
	// finish if the global context gets cancelled.
	goroutineTracker.Add(1)
	go func() {

		defer goroutineTracker.Done()

		for {
			select {
			case instanceEvent := <-s.InstanceEventChan:
				logger.Infof("Scaling algorithm received an instance database event with value: %v", instanceEvent)
				instance := instanceEvent.Data.(subscriptions.Instance)

				if instance.Status == "DRAINING" {

					// Track this goroutine so we can wait for it to
					// finish if the global context gets cancelled.
					goroutineTracker.Add(1)
					go func() {
						defer goroutineTracker.Done()

						// Create context for scaling operation
						scalingCtx, scalingCancel := context.WithCancel(context.Background())
						err := s.VerifyInstanceScaleDown(scalingCtx, instanceEvent, instance)

						// Cancel context once the operation is done
						scalingCancel()

						if err != nil {
							logger.Errorf("Error verifying instance scale down. Error: %v", err)
						}
					}()
				}
			case versionEvent := <-s.FrontendVersionChan:
				logger.Infof("Scaling algorithm received a frontend version database event with value: %v", versionEvent)
				version := versionEvent.Data.(subscriptions.FrontendVersion)

				// Track this goroutine so we can wait for it to
				// finish if the global context gets cancelled.
				goroutineTracker.Add(1)
				go func() {
					defer goroutineTracker.Done()

					// Create context for scaling operation
					scalingCtx, scalingCancel := context.WithCancel(context.Background())

					err := s.SwapOverImages(scalingCtx, versionEvent, version)

					// Cancel context once the operation is done
					scalingCancel()

					if err != nil {
						logger.Errorf("Error running image swapover. Error: %v", err)
					}
				}()

			case scheduledEvent := <-s.ScheduledEventChan:
				switch scheduledEvent.Type {
				case "SCHEDULED_SCALE_DOWN_EVENT":
					logger.Infof("Scaling algorithm received a scheduled scale down event with value: %v", scheduledEvent)

					goroutineTracker.Add(1)
					go func() {
						defer goroutineTracker.Done()

						scalingCtx, scalingCancel := context.WithCancel(context.Background())
						err := s.ScaleDownIfNecessary(scalingCtx, scheduledEvent)
						if err != nil {
							logger.Errorf("Error running scale down job on region %v. Err: %v", scheduledEvent.Region, err)
						}

						scalingCancel()
					}()
				case "SCHEDULED_IMAGE_UPGRADE_EVENT":
					logger.Infof("Scaling algorithm received an image upgrade event with value: %v", scheduledEvent)

					// Track this goroutine so we can wait for it to
					// finish if the global context gets cancelled.
					goroutineTracker.Add(1)
					go func() {
						defer goroutineTracker.Done()

						logger.Infof("%v", scheduledEvent)

						if scheduledEvent.Data == nil {
							logger.Errorf("Error running image upgrade, event data is nil.")
							return
						}

						scalingCtx, scalingCancel := context.WithCancel(context.Background())

						// Get arguments from scheduled event
						regionImageMap := scheduledEvent.Data.(map[string]interface{})

						err := s.UpgradeImage(scalingCtx, scheduledEvent, regionImageMap[scheduledEvent.Region])
						if err != nil {
							logger.Errorf("Error running image upgrade on region %v. Err: %v", scheduledEvent.Region, err)
						}

						scalingCancel()
					}()
				}
			case serverEvent := <-s.ServerEventChan:
				switch serverEvent.Type {
				case "SERVER_MANDELBOX_ASSIGN_EVENT":
					logger.Infof("Scaling algorithm received a mandelbox assign request with value: %v", serverEvent)

					goroutineTracker.Add(1)
					go func() {
						defer goroutineTracker.Done()
						serverEvent.Region = s.Region

						// Create context for scaling operation
						scalingCtx, scalingCancel := context.WithCancel(context.Background())

						err := s.MandelboxAssign(scalingCtx, serverEvent)
						if err != nil {
							logger.Errorf("Error running mandelbox assign action. Err: %v", err)
						}
						// Cancel context once the operation is done
						scalingCancel()
					}()
				}
			case <-globalCtx.Done():
				logger.Info("Global context has been cancelled. Exiting from default scaling algorithm event loop...")
				goroutineTracker.Wait()
				logger.Infof("Finished waiting for all goroutines to finish. Scaling algorithm from %v exited.", s.Region)
				return
			}
		}
	}()
}
