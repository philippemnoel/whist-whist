package hosts

import (
	"context"

	"github.com/whisthq/whist/backend/core-go/subscriptions"
)

type HostHandler interface {
	Initialize(region string) error
	SpinUpInstances(numInstances *int32, imageID string) (createdInstances []subscriptions.Instance, err error)
	SpinDownInstances(instanceIDs []string) (terminatedInstances []subscriptions.Instance, err error)
	WaitForInstanceTermination(context.Context, subscriptions.Instance) error
}
