package fractalcontainer // import "github.com/fractal/fractal/ecs-host-service/fractalcontainer"

import (
	logger "github.com/fractal/fractal/ecs-host-service/fractallogger"
)

// This file contains the code to track all FractalContainers. We _need to_ do
// this because we need to be able to look up containers by `FractalID` (and
// currently, `IdentifyingHostPort`, since the webserver is sending requests to the host
// service using `IdentifyingHostPort` instead of `FractalID`) in the top-level
// ecs-host-service package. We _can_ do this because FractalContainers can
// only be created using New(), since the underlying datatype is not exported.

// We optimize for the eventual case of needing to look up containers by
// `fractalID` _only_, not IdentifyingHostPort. Note: we don't need to lock
// `tracker`, since FractalIDs will (with extremely high probability) never
// overlap.
var tracker map[FractalID]FractalContainer = make(map[FractalID]FractalContainer)

func trackContainer(fc FractalContainer) {
	tracker[fc.GetFractalID()] = fc
}

func untrackContainer(fc FractalContainer) {
	delete(tracker, fc.GetFractalID())
}

// LookUpByIdentifyingHostPort finds a container by the port 32262/tcp.
func LookUpByIdentifyingHostPort(IdentifyingHostPort uint16) (FractalContainer, error) {
	for _, v := range tracker {
		p, _ := v.GetIdentifyingHostPort()
		if p == IdentifyingHostPort {
			return v, nil
		}
	}
	return nil, logger.MakeError("Couldn't find FractalContainer with IdentifyingHostPort %v", IdentifyingHostPort)
}

// LookUpByDockerID finds a container by its Docker ID.
func LookUpByDockerID(DockerID DockerID) (FractalContainer, error) {
	for _, v := range tracker {
		d := v.GetDockerID()
		if d == DockerID {
			return v, nil
		}
	}
	return nil, logger.MakeError("Couldn't find FractalContainer with DockerID %s", DockerID)
}

// CloseAll calls Close() on all tracked FractalContainers.
func CloseAll() {
	for _, v := range tracker {
		v.Close()
	}
}
