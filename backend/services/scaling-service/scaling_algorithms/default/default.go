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
	"sync"

	"github.com/whisthq/whist/backend/services/scaling-service/dbclient"
	"github.com/whisthq/whist/backend/services/scaling-service/hosts"
	"github.com/whisthq/whist/backend/services/subscriptions"
	"github.com/whisthq/whist/backend/services/utils"
	logger "github.com/whisthq/whist/backend/services/whistlogger"
	"go.uber.org/zap"
)

// ScalingAlgorithm is the basic abstraction of a scaling algorithm.
// It doesn't make any assumptions about the underlying implementation,
// it only defines the basic access methods that the event handler needs.
type ScalingAlgorithm interface {
	ProcessEvents(context.Context, *sync.WaitGroup)
	CreateEventChans()
	CreateGraphQLClient(subscriptions.WhistGraphQLClient)
	CreateDBClient(dbclient.WhistDBClient)
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

// ProcessEvents is the main function of the scaling algorithm, it is responsible of processing
// events and executing the appropiate scaling actions. This function is specific for each region
// scaling algorithm to be able to implement different strategies on each region.
func (s *DefaultScalingAlgorithm) ProcessEvents(globalCtx context.Context, goroutineTracker *sync.WaitGroup) {
	// Start algorithm main event loop
	// Track this goroutine so we can wait for it to
	// finish if the global context gets cancelled.
	goroutineTracker.Add(1)
	go func() {

		defer goroutineTracker.Done()

		for {
			select {
			case instanceEvent := <-s.InstanceEventChan:
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
							contextFields := []interface{}{
								zap.String("id", instanceEvent.ID),
								zap.Any("type", instanceEvent.Type),
								zap.Any("data", instanceEvent.Data),
								zap.String("region", instanceEvent.Region),
							}
							logger.Errorw(utils.Sprintf("error verifying instance scale down: %s", err), contextFields)
						}
					}()
				}
			case versionEvent := <-s.FrontendVersionChan:
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
						contextFields := []interface{}{
							zap.String("id", versionEvent.ID),
							zap.Any("type", versionEvent.Type),
							zap.Any("data", versionEvent.Data),
							zap.String("region", versionEvent.Region),
						}
						logger.Errorw(utils.Sprintf("error running image swapover: %s", err), contextFields)
					}
				}()

			case scheduledEvent := <-s.ScheduledEventChan:
				switch scheduledEvent.Type {
				case "SCHEDULED_SCALE_DOWN_EVENT":
					goroutineTracker.Add(1)
					go func() {
						defer goroutineTracker.Done()

						scalingCtx, scalingCancel := context.WithCancel(context.Background())
						err := s.ScaleDownIfNecessary(scalingCtx, scheduledEvent)
						if err != nil {
							contextFields := []interface{}{
								zap.String("id", scheduledEvent.ID),
								zap.Any("type", scheduledEvent.Type),
								zap.Any("data", scheduledEvent.Data),
								zap.String("region", scheduledEvent.Region),
							}
							logger.Errorw(utils.Sprintf("error running scale down job: %s", err), contextFields)
						}

						scalingCancel()
					}()
				case "SCHEDULED_IMAGE_UPGRADE_EVENT":
					// Track this goroutine so we can wait for it to
					// finish if the global context gets cancelled.
					goroutineTracker.Add(1)
					go func() {
						defer goroutineTracker.Done()

						if scheduledEvent.Data == nil {
							logger.Errorf("error running image upgrade, event data is nil.")
							return
						}

						scalingCtx, scalingCancel := context.WithCancel(context.Background())

						// Get arguments from scheduled event
						regionImageMap := scheduledEvent.Data.(map[string]interface{})

						err := s.UpgradeImage(scalingCtx, scheduledEvent, regionImageMap[scheduledEvent.Region])
						if err != nil {
							contextFields := []interface{}{
								zap.String("id", scheduledEvent.ID),
								zap.Any("type", scheduledEvent.Type),
								zap.Any("data", scheduledEvent.Data),
								zap.String("region", scheduledEvent.Region),
							}
							logger.Errorw(utils.Sprintf("error running image upgrade: %s", err), contextFields)
						}

						scalingCancel()
					}()
				}
			case serverEvent := <-s.ServerEventChan:
				switch serverEvent.Type {
				case "SERVER_MANDELBOX_ASSIGN_EVENT":
					goroutineTracker.Add(1)
					go func() {
						defer goroutineTracker.Done()
						serverEvent.Region = s.Region

						// Create context for scaling operation
						scalingCtx, scalingCancel := context.WithCancel(context.Background())

						err := s.MandelboxAssign(scalingCtx, serverEvent)
						if err != nil {
							contextFields := []interface{}{
								zap.String("id", serverEvent.ID),
								zap.Any("type", serverEvent.Type),
								zap.String("data", utils.Sprintf("%v", serverEvent.Data)),
								zap.String("region", serverEvent.Region),
							}
							logger.Errorw(utils.Sprintf("error running mandelbox assign action: %s", err), contextFields)
						}
						// Cancel context once the operation is done
						scalingCancel()
					}()
				}
			case <-globalCtx.Done():
				logger.Info("Global context has been cancelled. Exiting from default scaling algorithm event loop...")
				goroutineTracker.Wait()
				logger.Infof("Finished waiting for all goroutines to finish. Scaling algorithm %s exited.", s.Region)
				return
			}
		}
	}()
}
