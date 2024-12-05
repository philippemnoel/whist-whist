/*
Package mandelbox provides the abstractions of mandelboxes managed by Whist.
It defines the types and implements lots of functionality associated with
mandelboxes. In `tracker.go`, it also keeps a list of all mandelboxes on this
host at any given point.

This package, and its children, are meant to be low-level enough that it can
be imported by higher-level host service packages.

Note: Many of the methods that access the mandelbox data use a lock, please follow the
guidelines outlined below when writing code that directly uses the struct fields:

1. Are you accessing a mandelbox struct field directly? If so, lock.
2. Are you calling a method? If so, do not lock or you may cause a deadlock.
3. Are you inside a method? If so, assume the lock is unlocked on method entry.

Additionally, consider the following rules:
  - All locking should be done when and where the actual data access occurs.
    This means at the point of accessing the actual struct field
  - No method should assume that the lock is locked before entry
  - Getters and setters should always be used for struct field access when possible
*/
package mandelbox // import "github.com/whisthq/whist/backend/services/host-service/mandelbox"

import (
	"context"
	"path"
	"strings"
	"sync"
	"time"

	multierror "github.com/hashicorp/go-multierror"
	"github.com/whisthq/whist/backend/services/host-service/dbdriver"
	"github.com/whisthq/whist/backend/services/metadata"
	"github.com/whisthq/whist/backend/services/utils"
	logger "github.com/whisthq/whist/backend/services/whistlogger"

	"github.com/whisthq/whist/backend/services/host-service/mandelbox/gpus"
	"github.com/whisthq/whist/backend/services/host-service/mandelbox/portbindings"
	"github.com/whisthq/whist/backend/services/host-service/mandelbox/ttys"
	"github.com/whisthq/whist/backend/services/host-service/mandelbox/uinputdevices"
	"github.com/whisthq/whist/backend/services/types"

	dockercontainer "github.com/docker/docker/api/types/container"
)

// Mandelbox represents a mandelbox as it is kept track of in this
// package. Higher layers of the host-service use this interface.
type Mandelbox interface {
	GetID() types.MandelboxID

	AssignToUser(types.UserID)
	GetUserID() types.UserID

	GetSessionID() types.SessionID
	SetSessionID(session types.SessionID)

	// The status indicates the current state of the mandelbox lifecycle. Its
	// helpful when deciding if we should log some errors, backup configs, etc.
	// and for monitoring waiting mandelboxes.
	GetStatus() dbdriver.MandelboxStatus
	SetStatus(dbdriver.MandelboxStatus)

	// Return the time when the mandelbox status was last updated. The time
	// will be set internally when the status of the mandelbox changes.
	GetLastUpdatedTime() time.Time

	GetHostPort(mandelboxPort uint16, protocol portbindings.TransportProtocol) (uint16, error)
	GetIdentifyingHostPort() (uint16, error)

	InitializeTTY() error
	GetTTY() ttys.TTY

	AssignGPU() error
	GetGPU() gpus.Index

	// RegisterCreation is used to tell us the mapping between Docker IDs
	// and MandelboxIDs (which are used to track mandelboxes before they
	// are actually started, and therefore assigned a Docker runtime ID).
	// MandelboxIDs are also used to dynamically provide each mandelbox with a
	// directory that only that mandelbox has access to).
	RegisterCreation(types.DockerID) error
	SetAppName(types.AppName) error
	GetDockerID() types.DockerID
	GetAppName() types.AppName

	GetClientAppAccessToken() types.ClientAppAccessToken
	SetClientAppAccessToken(types.ClientAppAccessToken)

	// Methods for getting and registering the AES key for the mandelbox.
	// Its necessary to do so because the key has to be passed at container
	// launch and returned to the frontend.
	GetPrivateKey() types.PrivateKey
	SetPrivateKey(types.PrivateKey)

	// AssignPortBindings is used to request port bindings on the host for
	// mandelboxes. We allocate the host ports to be bound so the Docker runtime
	// can actually bind them into the mandelbox.
	AssignPortBindings([]portbindings.PortBinding) error
	GetPortBindings() []portbindings.PortBinding

	GetDeviceMappings() []dockercontainer.DeviceMapping
	InitializeUinputDevices(*sync.WaitGroup) error

	// Writes files containing the TTY assignment and host port
	// corresponding to port 32262/tcp in the mandelbox, in a directory
	// accessible only to this mandelbox. These data are special because
	// they are computed and written when the mandelbox is created.
	WriteMandelboxParams() error

	// WriteProtocolTimeouts writes files containing the protocol timeouts (i.e.
	// how long it will wait for a connection) to a directory accessible to only
	// this mandelbox.
	WriteProtocolTimeouts(protocolConnectTimeout int, protocolDisconnectTimeout int) error

	// MarkParamsReady indicates that processes are ready to be initialized.
	MarkParamsReady() error

	// WriteSessionID writes the session ID received from the client to a file
	// so it can be used for logging.
	WriteSessionID() error

	// WriteJSONData writes arbitrary data from JSON Transport endpoint for
	// mandelbox internals.
	WriteJSONData(types.JSONData) error

	// GetContext provides the context corresponding to this specific mandelbox.
	GetContext() context.Context

	// Close cancels the mandelbox-specific context, triggering the cleanup of
	// all associated resources.
	Close()
}

// New creates a new Mandelbox given a parent context and a whist ID. It is
// simply a wrapper around new() to avoid exposing the underlying type to
// non-testing packages.
func New(baseCtx context.Context, goroutineTracker *sync.WaitGroup, fid types.MandelboxID, mandelboxDieChan chan bool) Mandelbox {
	return new(baseCtx, goroutineTracker, fid, mandelboxDieChan)
}

func new(baseCtx context.Context, goroutineTracker *sync.WaitGroup, fid types.MandelboxID, mandelboxDieChan chan bool) *mandelboxData {
	// We create a context for this mandelbox specifically.
	ctx, cancel := context.WithCancel(baseCtx)

	mandelbox := &mandelboxData{
		ctx:                  ctx,
		cancel:               cancel,
		ID:                   fid,
		uinputDeviceMappings: []dockercontainer.DeviceMapping{},
		otherDeviceMappings:  []dockercontainer.DeviceMapping{},
		updatedAt:            time.Now(),
		mandelboxDieChan:     mandelboxDieChan,
	}

	err := mandelbox.createResourceMappingDir()
	if err != nil {
		logger.Errorf("couldn't create mandelbox resource mapping directory: %s", err)
	}

	trackMandelbox(mandelbox)

	// Mount /dev/fuse for in-mandelbox FUSE filesystems to work
	mandelbox.otherDeviceMappings = append(mandelbox.otherDeviceMappings, dockercontainer.DeviceMapping{
		PathOnHost:        "/dev/fuse",
		PathInContainer:   "/dev/fuse",
		CgroupPermissions: "rwm", // read, write, mknod (the default)
	})

	// We start a goroutine that cleans up this Mandelbox as soon as its
	// context is cancelled. This ensures that we automatically clean up when
	// either `Close()` is called for this mandelbox, or the global context is
	// cancelled.
	goroutineTracker.Add(1)
	go func() {
		defer goroutineTracker.Done()

		<-ctx.Done()

		var mandelboxCloseErr *multierror.Error

		// Don't try to mark mandelbox as dying if the status is empty. This indicates that
		// the mandelbox wasn't created successfully in the spinup process.
		if mandelbox.GetStatus() != "" {
			if err := dbdriver.WriteMandelboxStatus(mandelbox.GetID(), dbdriver.MandelboxStatusDying); err != nil {
				mandelboxCloseErr = multierror.Append(mandelboxCloseErr, err)
			}
		} else {
			logger.Infof("Not marking mandelbox as DYING since it doesn't exist in database")
		}

		untrackMandelbox(mandelbox)
		logger.Infof("Successfully untracked mandelbox %s", mandelbox.GetID())

		mandelbox.rwlock.Lock()

		// Free port bindings
		portbindings.Free(mandelbox.portBindings)
		mandelbox.portBindings = nil
		logger.Infof("Successfully freed port bindings for mandelbox %s", mandelbox.GetID())

		// Free uinput devices
		mandelbox.uinputDevices.Close()
		mandelbox.uinputDevices = nil
		mandelbox.uinputDeviceMappings = []dockercontainer.DeviceMapping{}
		logger.Infof("Successfully freed uinput devices for mandelbox %s", mandelbox.GetID())

		// Free TTY
		ttys.Free(mandelbox.tty)
		logger.Infof("Successfully freed TTY %v for mandelbox %s", mandelbox.tty, mandelbox.GetID())
		mandelbox.tty = 0

		// CI does not have GPUs
		if !metadata.IsRunningInCI() && metadata.IsGPU() {
			if err := gpus.Free(mandelbox.gpuIndex, mandelbox.GetID()); err != nil {
				mandelboxCloseErr = multierror.Append(mandelboxCloseErr, utils.MakeError("error freeing GPU %v for mandelbox %s: %s", mandelbox.gpuIndex, mandelbox.GetID(), err))
			} else {
				logger.Infof("Successfully freed GPU %v for mandelbox %s", mandelbox.gpuIndex, mandelbox.GetID())
			}
		}

		mandelbox.rwlock.Unlock()

		// Clean resource mappings
		mandelbox.cleanResourceMappingDir()
		logger.Infof("Successfully cleaned resource mapping dir for mandelbox %s", mandelbox.GetID())

		// Don't try to remove mandelbox if the status is empty. This indicates that
		// the mandelbox wasn't created successfully in the spinup process.
		if mandelbox.GetStatus() != "" {
			if err := dbdriver.RemoveMandelbox(mandelbox.GetID()); err != nil {
				mandelboxCloseErr = multierror.Append(mandelboxCloseErr, err)
			}
		} else {
			logger.Infof("Not removing mandelbox from database since it doesn't exist")
		}

		logger.Infof("Cleaned up after Mandelbox %s", mandelbox.GetID())

		if mandelboxCloseErr != nil {
			logger.Errorf("cleaned up after mandelbox, %s", mandelboxCloseErr)
		}

		// Only after every step of the cleanup function has completed successfully,
		// notify that the cleanup for this mandelbox is done.
		mandelbox.mandelboxDieChan <- true
	}()

	return mandelbox
}

type mandelboxData struct {
	// TODO: re-evaluate whether we should still be doing this
	// Note: while the Go language authors discourage storing a context in a
	// struct, we need to do this to be able to call mandelbox methods from both
	// the host-service and ecsagent (NOTE: we no longer use the ECS agent, this
	// can probably be reworked).
	ctx    context.Context
	cancel context.CancelFunc

	ID types.MandelboxID

	// We use rwlock to protect all the below fields.
	rwlock sync.RWMutex

	dockerID  types.DockerID
	appName   types.AppName
	userID    types.UserID
	sessionID types.SessionID
	status    dbdriver.MandelboxStatus
	updatedAt time.Time
	tty       ttys.TTY
	gpuIndex  gpus.Index

	clientAppAccessToken types.ClientAppAccessToken
	privateKey           types.PrivateKey

	uinputDevices        *uinputdevices.UinputDevices
	uinputDeviceMappings []dockercontainer.DeviceMapping
	// We use this to mount devices like /dev/fuse
	otherDeviceMappings []dockercontainer.DeviceMapping

	portBindings []portbindings.PortBinding

	mandelboxDieChan chan bool
}

// GetID returns the mandelbox ID.
// No need to lock this since this should be treated as immutable after creation.
func (mandelbox *mandelboxData) GetID() types.MandelboxID {
	return mandelbox.ID
}

// AssignToUser assigns the mandelbox to the given user.
func (mandelbox *mandelboxData) AssignToUser(u types.UserID) {
	mandelbox.rwlock.Lock()
	defer mandelbox.rwlock.Unlock()
	mandelbox.userID = u
}

// GetUserID returns the ID of the user assigned to the mandelbox.
func (mandelbox *mandelboxData) GetUserID() types.UserID {
	mandelbox.rwlock.RLock()
	defer mandelbox.rwlock.RUnlock()
	return mandelbox.userID
}

// GetSessionID returns the ID of the session running on the mandelbox.
func (mandelbox *mandelboxData) GetSessionID() types.SessionID {
	mandelbox.rwlock.RLock()
	defer mandelbox.rwlock.RUnlock()
	return mandelbox.sessionID
}

// SetSessionID sets the ID of the session running on the mandelbox.
func (mandelbox *mandelboxData) SetSessionID(session types.SessionID) {
	mandelbox.rwlock.Lock()
	defer mandelbox.rwlock.Unlock()
	mandelbox.sessionID = session
}

// GetConnectedStatus returns the current status of the mandelbox.
func (mandelbox *mandelboxData) GetStatus() dbdriver.MandelboxStatus {
	mandelbox.rwlock.RLock()
	defer mandelbox.rwlock.RUnlock()
	return mandelbox.status
}

// SetConnectedStatus sets the mandelbox status and changes the time
// the mandelbox was updated.
func (mandelbox *mandelboxData) SetStatus(status dbdriver.MandelboxStatus) {
	mandelbox.rwlock.Lock()
	defer mandelbox.rwlock.Unlock()
	mandelbox.status = status
	mandelbox.updatedAt = time.Now()
}

// GetLastUpdatedTime returns the time when the mandelbox status was last updated
func (mandelbox *mandelboxData) GetLastUpdatedTime() time.Time {
	mandelbox.rwlock.RLock()
	defer mandelbox.rwlock.RUnlock()
	return mandelbox.updatedAt
}

// GetPrivateKey returns the private key.
func (mandelbox *mandelboxData) GetPrivateKey() types.PrivateKey {
	mandelbox.rwlock.RLock()
	defer mandelbox.rwlock.RUnlock()
	return mandelbox.privateKey
}

// SetPrivateKey sets the private key.
func (mandelbox *mandelboxData) SetPrivateKey(aesKey types.PrivateKey) {
	mandelbox.rwlock.Lock()
	defer mandelbox.rwlock.Unlock()
	mandelbox.privateKey = aesKey
}

// GetClientAppAccessToken returns the Whist frontend access token.
func (mandelbox *mandelboxData) GetClientAppAccessToken() types.ClientAppAccessToken {
	mandelbox.rwlock.RLock()
	defer mandelbox.rwlock.RUnlock()
	return mandelbox.clientAppAccessToken
}

// SetClientAppAccessToken sets the Whist frontend access token.
func (mandelbox *mandelboxData) SetClientAppAccessToken(token types.ClientAppAccessToken) {
	mandelbox.rwlock.Lock()
	defer mandelbox.rwlock.Unlock()
	mandelbox.clientAppAccessToken = token
}

// GetHostPort returns the assigned host port for the given mandelbox
// port and protocol.
func (mandelbox *mandelboxData) GetHostPort(mandelboxPort uint16, protocol portbindings.TransportProtocol) (uint16, error) {
	binds := mandelbox.GetPortBindings()
	for _, b := range binds {
		if b.Protocol == protocol && b.MandelboxPort == mandelboxPort {
			return b.HostPort, nil
		}
	}

	return 0, utils.MakeError("couldn't GetHostPort(%v, %v) for mandelbox with MandelboxID %s", mandelboxPort, protocol, mandelbox.GetID())
}

// GetIdentifyingHostPort returns the assigned host port for TCP 32262.
func (mandelbox *mandelboxData) GetIdentifyingHostPort() (uint16, error) {
	return mandelbox.GetHostPort(32262, portbindings.TransportProtocolTCP)
}

// AssignGPU tries to assign the mandelbox a GPU.
func (mandelbox *mandelboxData) AssignGPU() error {
	gpu, err := gpus.Allocate(mandelbox.GetID())
	if err != nil {
		return err
	}

	mandelbox.rwlock.Lock()
	defer mandelbox.rwlock.Unlock()

	mandelbox.gpuIndex = gpu
	return nil
}

// GetGPU returns the assigned GPU index.
func (mandelbox *mandelboxData) GetGPU() gpus.Index {
	mandelbox.rwlock.RLock()
	defer mandelbox.rwlock.RUnlock()
	return mandelbox.gpuIndex
}

// InitializeTTY tries to assign the mandelbox a TTY.
func (mandelbox *mandelboxData) InitializeTTY() error {
	tty, err := ttys.Allocate()
	if err != nil {
		return err
	}

	mandelbox.rwlock.Lock()
	defer mandelbox.rwlock.Unlock()

	mandelbox.tty = tty
	return nil
}

// GetTTY returns the assigned TTY.
func (mandelbox *mandelboxData) GetTTY() ttys.TTY {
	mandelbox.rwlock.RLock()
	defer mandelbox.rwlock.RUnlock()
	return mandelbox.tty
}

// RegisterCreation registers a Docker container ID to the mandelbox.
func (mandelbox *mandelboxData) RegisterCreation(d types.DockerID) error {
	if len(d) == 0 {
		return utils.MakeError("can't register mandelbox %s with empty docker ID", mandelbox.GetUserID())
	}

	mandelbox.rwlock.Lock()
	defer mandelbox.rwlock.Unlock()

	mandelbox.dockerID = d
	return nil
}

// SetAppName tries to set the app name for the mandelbox.
func (mandelbox *mandelboxData) SetAppName(name types.AppName) error {
	if len(name) == 0 {
		return utils.MakeError("can't set mandelbox app name to empty for mandelboxID: %s", mandelbox.GetUserID())
	}

	mandelbox.rwlock.Lock()
	defer mandelbox.rwlock.Unlock()

	mandelbox.appName = name
	return nil
}

// GetDockerID returns the Docker container ID for the mandelbox.
func (mandelbox *mandelboxData) GetDockerID() types.DockerID {
	mandelbox.rwlock.RLock()
	defer mandelbox.rwlock.RUnlock()
	return mandelbox.dockerID
}

// GetAppName returns the app name for the mandelbox.
func (mandelbox *mandelboxData) GetAppName() types.AppName {
	mandelbox.rwlock.RLock()
	defer mandelbox.rwlock.RUnlock()
	return mandelbox.appName
}

// AssignPortBindings tries to assign the desired port bindings to the mandelbox.
func (mandelbox *mandelboxData) AssignPortBindings(desired []portbindings.PortBinding) error {
	result, err := portbindings.Allocate(desired)
	if err != nil {
		return err
	}

	mandelbox.rwlock.Lock()
	defer mandelbox.rwlock.Unlock()

	mandelbox.portBindings = result
	return nil
}

// GetPortBindings returns the assigned port bindings.
func (mandelbox *mandelboxData) GetPortBindings() []portbindings.PortBinding {
	mandelbox.rwlock.RLock()
	defer mandelbox.rwlock.RUnlock()
	return mandelbox.portBindings
}

// GetDeviceMappings returns all device mapping for the mandelbox.
func (mandelbox *mandelboxData) GetDeviceMappings() []dockercontainer.DeviceMapping {
	mandelbox.rwlock.RLock()
	defer mandelbox.rwlock.RUnlock()
	return append(mandelbox.uinputDeviceMappings, mandelbox.otherDeviceMappings...)
}

// InitializeUinputDevices tries to assign uinput devices to the mandelbox.
func (mandelbox *mandelboxData) InitializeUinputDevices(goroutineTracker *sync.WaitGroup) error {
	devices, mappings, err := uinputdevices.Allocate()
	if err != nil {
		return utils.MakeError("couldn't allocate uinput devices: %s", err)
	}

	mandelbox.rwlock.Lock()
	defer mandelbox.rwlock.Unlock()

	mandelbox.uinputDevices = devices
	mandelbox.uinputDeviceMappings = mappings

	goroutineTracker.Add(1)
	go func() {
		defer goroutineTracker.Done()
		socketPath := path.Join(utils.TempDir, mandelbox.GetID().String(), "/sockets/uinput.sock")
		err := uinputdevices.SendDeviceFDsOverSocket(mandelbox.ctx, goroutineTracker, devices, socketPath)
		if err != nil {
			placeholderUUID := types.MandelboxID(utils.PlaceholderWarmupUUID())
			if mandelbox.GetID() == placeholderUUID && strings.Contains(err.Error(), "use of closed network connection") ||
				mandelbox.GetStatus() != dbdriver.MandelboxStatusRunning {
				logger.Warningf("SendDeviceFDsOverSocket returned for MandelboxID %s with error: %s", mandelbox.GetID(), err)
			} else {
				logger.Errorf("sendDeviceFDsOverSocket returned for MandelboxID %s with error: %s", mandelbox.GetID(), err)
			}
		} else {
			logger.Infof("sendDeviceFDsOverSocket returned successfully for MandelboxID %s", mandelbox.GetID())
		}
	}()

	return nil
}

// GetContext returns the context for the mandelbox.
func (mandelbox *mandelboxData) GetContext() context.Context {
	mandelbox.rwlock.RLock()
	defer mandelbox.rwlock.RUnlock()
	return mandelbox.ctx
}

// Close cancels the context for the mandelbox, causing it to shutdown
// and clean up all its resources.
func (mandelbox *mandelboxData) Close() {
	// Cancel context, triggering the freeing up of all resources, including
	// tracked by goroutines
	mandelbox.cancel()
}
