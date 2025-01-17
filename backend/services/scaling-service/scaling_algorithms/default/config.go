package scaling_algorithms

import (
	"time"

	"github.com/whisthq/whist/backend/services/constants"
	"github.com/whisthq/whist/backend/services/utils"
)

// defaultInstanceBuffer is the number of instances with space to run
// mandelboxes. This value is used when deciding how many instances to
// scale up if we don't have enough capacity.
var defaultInstanceBuffer int = 1

const (
	// The override value used when assigning users in dev, for developer convenience.
	CLIENT_COMMIT_HASH_DEV_OVERRIDE = "local_dev"

	// These are all the possible reasons we would fail to find an instance for a user
	// and return a 503 error

	// Instance was found but the frontend is out of date
	COMMIT_HASH_MISMATCH = "COMMIT_HASH_MISMATCH"

	// No instance was found e.g. a capacity issue
	NO_INSTANCE_AVAILABLE = "NO_INSTANCE_AVAILABLE"

	// The requested region(s) have not been enabled
	REGION_NOT_ENABLED = "REGION_NOT_ENABLED"

	// User is already connected to a mandelbox, possibly on another device
	USER_ALREADY_ACTIVE = "USER_ALREADY_ACTIVE"

	// A generic 503 error message
	SERVICE_UNAVAILABLE = "SERVICE_UNAVAILABLE"
)

// VCPUsPerMandelbox indicates the number of vCPUs allocated per mandelbox.
const VCPUsPerMandelbox = 4

// A map containing how many GPUs each instance type has.
var instanceTypeToGPUNum = map[string]int{
	"g4dn.xlarge":   1,
	"g4dn.2xlarge":  1,
	"g4dn.4xlarge":  1,
	"g4dn.8xlarge":  1,
	"g4dn.16xlarge": 1,
	"g4dn.12xlarge": 4,
}

// A map containing how many VCPUs each instance type has.
var instanceTypeToVCPUNum = map[string]int{
	"g4dn.xlarge":   4,
	"g4dn.2xlarge":  8,
	"g4dn.4xlarge":  16,
	"g4dn.8xlarge":  32,
	"g4dn.16xlarge": 64,
	"g4dn.12xlarge": 48,
}

// instanceCapacity is a mapping of the mandelbox capacity each type of instance has.
var instanceCapacity = generateInstanceCapacityMap(instanceTypeToGPUNum, instanceTypeToVCPUNum)

var (
	// maxWaitTimeReady is the max time we whould wait for instances to be ready.
	maxWaitTimeReady = 5 * time.Minute
	// maxWaitTimeTerminated is the max time we whould wait for instances to be terminated.
	maxWaitTimeTerminated = 5 * time.Minute
)

// generateInstanceCapacityMap uses the global instanceTypeToGPUNum and instanceTypeToVCPUNum maps
// to generate the maximum mandelbox capacity for each instance type in the intersection
// of their keys.
func generateInstanceCapacityMap(instanceToGPUMap, instanceToVCPUMap map[string]int) map[string]int {
	// Initialize the instance capacity map
	capacityMap := map[string]int{}
	for instanceType, gpuNum := range instanceToGPUMap {
		// Only populate for instances that are in both maps
		vcpuNum, ok := instanceToVCPUMap[instanceType]
		if !ok {
			continue
		}
		min := utils.Min(gpuNum*constants.MaxMandelboxesPerGPU, vcpuNum/VCPUsPerMandelbox)
		capacityMap[instanceType] = min
	}
	return capacityMap
}
