//go:build linux

package metrics

import (
	"github.com/whisthq/whist/backend/services/utils"
	logger "github.com/whisthq/whist/backend/services/whistlogger"

	"github.com/NVIDIA/go-nvml/pkg/nvml"
)

func initializeGPUMetricsCollector() error {
	if nvmlRet := nvml.Init(); nvmlRet != nvml.SUCCESS {
		return utils.MakeError("Unable to initialize NVML for metrics collection.")
	}
	return nil
}

func shutdownGPUMetricsCollector() {
	if nvmlRet := nvml.Shutdown(); nvmlRet != nvml.SUCCESS {
		logger.Errorf("Error shutting down NVML library.")
	}
}

func (newMetrics *RuntimeMetrics) collectGPUMetrics() []error {
	if numGPUs, nvmlRet := nvml.DeviceGetCount(); nvmlRet != nvml.SUCCESS {
		newMetrics.NumberOfGPUs = -1
		return []error{utils.MakeError("Error getting GPU count")}
	} else {
		newMetrics.NumberOfGPUs = numGPUs
		errs := make([]error, 0)

		var totalVramKB uint64 = 0
		var usedVramKB uint64 = 0
		var totalKernelUtilPercent uint32 = 0
		var totalMemBandwidthUtilPercent uint32 = 0
		for i := 0; i < numGPUs; i++ {
			if device, nvmlRet := nvml.DeviceGetHandleByIndex(i); nvmlRet != nvml.SUCCESS {
				errs = append(errs, utils.MakeError("Couldn't get NVIDIA device at index %v", i))
				break
			} else {
				if memInfo, nvmlRet := nvml.DeviceGetMemoryInfo(device); nvmlRet != nvml.SUCCESS {
					errs = append(errs, utils.MakeError("Couldn't get video memory info for device at NVML index %v", i))
					break
				} else {
					totalVramKB += memInfo.Total / 1024
					usedVramKB += memInfo.Used / 1024
					// For some reason, NVML always returns 0 for memInfo.Free, so we do
					// the free calculation ourselves below.
				}

				if util, nvmlRet := nvml.DeviceGetUtilizationRates(device); nvmlRet != nvml.SUCCESS {
					errs = append(errs, utils.MakeError("Couldn't get utilization info for device at NVML index %v", i))
					break
				} else {
					totalKernelUtilPercent += util.Gpu
					totalMemBandwidthUtilPercent += util.Memory
				}
			}
		}

		newMetrics.TotalVideoMemoryKB = totalVramKB
		newMetrics.UsedVideoMemoryKB = usedVramKB
		newMetrics.FreeVideoMemoryKB = totalVramKB - usedVramKB

		newMetrics.GPUKernelUtilizationPercent = float64(totalKernelUtilPercent) / float64(numGPUs)
		newMetrics.GPUMemoryBandwidthUtilizationPercent = float64(totalMemBandwidthUtilPercent) / float64(numGPUs)
		return errs
	}
}
