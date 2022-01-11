package scaling_algorithms

import (
	"sync"

	"github.com/whisthq/whist/backend/core-go/subscriptions"
	logger "github.com/whisthq/whist/backend/core-go/whistlogger"
	"github.com/whisthq/whist/backend/scaling-service/hosts"
	aws "github.com/whisthq/whist/backend/scaling-service/hosts/aws"
)

// ScalingAlgorithm is the basic abstraction of the scaling service
// that receives a stream of events and makes calls to the host handler.
type ScalingAlgorithm interface {
	ProcessEvents(*sync.WaitGroup)
	CreateEventChans()
	CreateBuffer()
	CreateGraphQLClient(*subscriptions.GraphQLClient)
}

// ScalingEvent is an event that contains all the relevant information
// to make scaling decisions.
// Idea: We could use UUIDs for each event so we can improve our logging
// and debugging capabilities.
type ScalingEvent struct {
	Type   interface{} // The type of event (database, timing, etc.)
	Data   interface{} // Data relevant to the event
	Region string      // Region where the scaling will be performed
}

// DefaultScalingAlgorithm abstracts the shared functionalities to be used
// by all of the different, region-based scaling algorithms.
type DefaultScalingAlgorithm struct {
	Host               hosts.HostHandler
	GraphQLClient      *subscriptions.GraphQLClient
	InstanceBuffer     *int32
	Region             string
	InstanceEventChan  chan ScalingEvent
	ImageEventChan     chan ScalingEvent
	ScheduledEventChan chan ScalingEvent
}

// CreateEventChans creates the event channels if they don't alredy exist.
func (s *DefaultScalingAlgorithm) CreateEventChans() {
	// TODO: Only use one chan for database events and one for scheduled events
	if s.InstanceEventChan == nil {
		s.InstanceEventChan = make(chan ScalingEvent, 100)
	}
	if s.ImageEventChan == nil {
		s.ImageEventChan = make(chan ScalingEvent, 100)
	}
	if s.ScheduledEventChan == nil {
		s.ScheduledEventChan = make(chan ScalingEvent, 100)
	}
}

// CreateBuffer initializes the instance buffer.
func (s *DefaultScalingAlgorithm) CreateBuffer() {
	buff := int32(DEFAULT_INSTANCE_BUFFER)
	s.InstanceBuffer = &buff
}

// CreateGraphQLClient sets the graphqlClient to be used on queries.
func (s *DefaultScalingAlgorithm) CreateGraphQLClient(client *subscriptions.GraphQLClient) {
	if s.GraphQLClient == nil {
		s.GraphQLClient = client
	}
}

// ProcessEvents is the main function of the scaling algorithm, it is responsible of processing
// events and executing the appropiate scaling actions. This function is specific for each region
// scaling algorithm to be able to implement different strategies on each region.
func (s *DefaultScalingAlgorithm) ProcessEvents(goroutineTracker *sync.WaitGroup) {
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
	goroutineTracker.Add(1)
	go func() {

		for {
			logger.Infof("Scaling algorithm listening for events...")

			select {
			case instanceEvent := <-s.InstanceEventChan:
				logger.Infof("Scaling algorithm received an instance database event with value: %v", instanceEvent)
			case imageEvent := <-s.ImageEventChan:
				logger.Infof("Scaling algorithm received an image database event with value: %v", imageEvent)
			case scheduledEvent := <-s.ScheduledEventChan:
				switch scheduledEvent.Type {
				case "SCHEDULED_SCALE_DOWN":
					logger.Infof("Scaling algorithm received a scheduled scale down event with value: %v", scheduledEvent)
				}
			}
		}
	}()

}
