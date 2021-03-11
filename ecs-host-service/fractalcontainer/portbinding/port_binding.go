package portbinding // import "github.com/fractal/fractal/ecs-host-service/fractalcontainer/portbinding"

import (
	"math/rand"
	"sync"

	logger "github.com/fractal/fractal/ecs-host-service/fractallogger"
)

// A PortBinding represents a single port that is bound inside a container to a
// port with the same protocol on the host.
type PortBinding struct {
	// ContainerPort is the port inside the container
	ContainerPort uint16
	// HostPort is the port exposed on the host
	HostPort uint16
	// BindIP is the IP address to which the port is bound
	BindIP string `json:"BindIp"`
	// Protocol is the protocol of the port
	Protocol transportProtocol
}

type portStatus byte

const (
	// Obtained from reading though Docker daemon source code
	minAllowedPort = 1025  // inclusive
	maxAllowedPort = 49151 // exclusive

	reserved portStatus = iota
	inUse
)

// Reserve lets us mark certain ports as "reserved" so they don't get
// allocated for containers. This needs to be called at program initialization
// (ideally in an `init` function), before any containers are started.
func Reserve(num uint16, protocol transportProtocol) {
	mapToUse, err := getProtocolSpecificHostPortMap(protocol)
	if err != nil {
		logger.Panic(err)
	}
	(*mapToUse)[num] = reserved
	logger.Infof("Marked Port %v/%s as reserved", num, protocol)
}

// Unexported global maps to keep track of free vs allocated host ports. If the
// key exists in the map, that means that port is unavailable. In particular,
// if the value is `reserved`, then that port is permanently available; else,
// it is `inUse`, meaning in use by a given container.
type protocolSpecificHostPortMap map[uint16]portStatus

var tcpPortMap protocolSpecificHostPortMap = make(map[uint16]portStatus)
var udpPortMap protocolSpecificHostPortMap = make(map[uint16]portStatus)

// Lock to protect `tcpPortMap` and `udpPortMap`
var portMapsLock = new(sync.Mutex)

// allocateSinglePort allocates a single port given a desired binding. It
// requires that `portMapsLock` is held throughout. Just like
// `AllocatePortBindings()`, it interprets a zero value for
// `desiredBind.HostPort` as a request to allocate any host port, and a nonzero
// value as a request to allocate that specific host port.
func allocateSinglePort(desiredBind PortBinding) (PortBinding, error) {
	mapToUse, err := getProtocolSpecificHostPortMap(desiredBind.Protocol)
	if err != nil {
		return desiredBind, logger.MakeError("allocateSinglePort failed. Error: %s", err)
	}

	// If the given HostPort is nonzero, we want to use that one specifically.
	// Else, we have to randomly allocate a free one.
	if desiredBind.HostPort != 0 {
		// Check that the desired port is actually in the allowed range
		if !isInAllowedRange(desiredBind.HostPort) {
			return desiredBind, logger.MakeError("allocateSinglePort: received a request to allocate a disallowed port: %v/%s", desiredBind.HostPort, desiredBind.Protocol)
		}

		// Check that this port isn't already allocated to a container, or reserved
		if _, exists := (*mapToUse)[desiredBind.HostPort]; exists {
			return desiredBind, logger.MakeError("allocateSinglePort: Could not allocate HostPort %v/%v: already bound or reserved.", desiredBind.HostPort, desiredBind.Protocol)
		}

		// Mark it as allocated and return
		(*mapToUse)[desiredBind.HostPort] = inUse
		return desiredBind, nil

	} else {
		// Gotta allocate a port ourselves
		randomPort := randomPortInAllowedRange()
		numTries := 0
		for _, exists := (*mapToUse)[randomPort]; exists; randomPort = randomPortInAllowedRange() {
			numTries++
			if numTries >= 100 {
				return desiredBind, logger.MakeError("Tried %v times to allocate a random host port for container port %v/%v. Breaking out after %v tries to avoid spinning for too long.", numTries, desiredBind.HostPort, desiredBind.Protocol, numTries)
			}
		}

		// Mark it as allocated and return
		(*mapToUse)[randomPort] = inUse
		return PortBinding{
			ContainerPort: desiredBind.ContainerPort,
			HostPort:      randomPort,
			Protocol:      desiredBind.Protocol,
			BindIP:        desiredBind.BindIP,
		}, nil
	}
}

// freeSinglePort marks the given port as free in the correct `protocolSpecificHostPortMap`. If the given port is reserved, it logs an error. This function requires that `portMapsLock` is held throughout.
func freeSinglePort(bind PortBinding) {
	mapToUse, err := getProtocolSpecificHostPortMap(bind.Protocol)
	if err != nil {
		logger.Errorf("FreePortBindings: failed for bind %v. Error: %s", bind, err)
	}

	v, ok := (*mapToUse)[bind.HostPort]
	if !ok {
		logger.Errorf("FreePortBindings: tried to remove nonexistent binding for %v/%v.", bind.HostPort, bind.Protocol)
	} else if v == reserved {
		logger.Errorf("FreePortBindings: tried to remove reserved binding for %v/%v. Sorry, but I can't let you do that.", bind.HostPort, bind.Protocol)
	} else {
		delete(*mapToUse, bind.HostPort)
	}
}

// `Free` marks all provided and non-reserved host ports as free in `tcpPortMap`
// and `udpPortMap`.
func Free(binds []PortBinding) {
	if len(binds) == 0 {
		return
	}

	portMapsLock.Lock()
	defer portMapsLock.Unlock()

	for _, bind := range binds {
		freeSinglePort(bind)
	}
}

// Allocate allocates the desired port bindings atomically. In other words, it
// will either successfully allocate all of them, or none of them.
func Allocate(desiredBinds []PortBinding) ([]PortBinding, error) {
	portMapsLock.Lock()
	defer portMapsLock.Unlock()

	var result = make([]PortBinding, 0, len(desiredBinds))
	var reterr error = nil

	for _, v := range desiredBinds {
		var b PortBinding
		b, reterr = allocateSinglePort(v)
		if reterr != nil {
			break
		} else {
			result = append(result, b)
		}
	}

	// If one allocation failed, we don't want to leak any ports.
	if reterr != nil {
		for _, v := range result {
			freeSinglePort(v)
		}
		return nil, reterr
	}

	return result, nil
}

// Helper function to generate random port values in the allowed range
func randomPortInAllowedRange() uint16 {
	return uint16(minAllowedPort + rand.Intn(maxAllowedPort-minAllowedPort))
}

// Helper function to determine whether a given port is in the allowed range, or not
func isInAllowedRange(p uint16) bool {
	return p >= minAllowedPort && p < maxAllowedPort
}

// Helper function to check that a `fractaltypes.PortBinding` is valid (i.e.
// either "tcp" or "udp"), and return a pointer to the correct map to
// read/modify (i.e. either `tcpPorts` or `udpPorts`).
func getProtocolSpecificHostPortMap(protocol transportProtocol) (*protocolSpecificHostPortMap, error) {
	switch protocol {
	case TransportProtocolTCP:
		return &tcpPortMap, nil
	case TransportProtocolUDP:
		return &udpPortMap, nil
	default:
		return nil, logger.MakeError("getProtocolSpecificHostPortMap: received incorrect protocol: %v", protocol)
	}
}
