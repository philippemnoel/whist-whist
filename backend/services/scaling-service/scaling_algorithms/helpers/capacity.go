// Copyright (c) 2021-2022 Whist Technologies, Inc.

/*
Package helpers contains utility functions that can be shared between actions and
algorithms that handle sanitizing, parsing and computing values. It also includes
some functions that make it easier to test the scaling service locally.
*/

package helpers

import (
	"github.com/whisthq/whist/backend/services/subscriptions"
)

// ComputeRealMandelboxCapacity is a helper function to compute the real mandelbox capacity.
// Real capcity is the number of free mandelboxes available on instances with active status.
func ComputeRealMandelboxCapacity(imageID string, activeInstances []subscriptions.Instance) int {
	var (
		realMandelboxCapacity int
	)

	// Loop over active instances (status ACTIVE), only consider the ones with the current image
	for _, instance := range activeInstances {
		if instance.ImageID == imageID {
			realMandelboxCapacity += int(instance.RemainingCapacity)
		}
	}

	return realMandelboxCapacity
}

// ComputeExpectedMandelboxCapacity is a helper function to compute the expected mandelbox capacity.
// Expected capacity is the number of free mandelboxes available on instances with active status and
// in starting instances.
func ComputeExpectedMandelboxCapacity(imageID string, activeInstances []subscriptions.Instance, startingInstances []subscriptions.Instance) int {
	var (
		realMandelboxCapacity     int
		expectedMandelboxCapacity int
	)

	// Get the capacity from active instances
	realMandelboxCapacity = ComputeRealMandelboxCapacity(imageID, activeInstances)

	// Loop over starting instances (status PRE_CONNECTION), only consider the ones with the current image
	for _, instance := range startingInstances {
		if instance.ImageID == imageID {
			expectedMandelboxCapacity += int(instance.RemainingCapacity)
		}
	}

	expectedMandelboxCapacity += realMandelboxCapacity
	return expectedMandelboxCapacity
}
