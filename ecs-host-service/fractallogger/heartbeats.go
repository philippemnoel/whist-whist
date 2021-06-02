package fractallogger // import "github.com/fractal/fractal/ecs-host-service/fractallogger"

import (
	"bytes"
	"encoding/json"
	"io/ioutil"
	"math/rand"
	"net/http"
	"time"
)

// We implement

// Fractal webserver URLs and relevant webserver endpoints
const localdevFractalWebserver = "https://127.0.0.1:7730"
const devFractalWebserver = "https://dev-server.fractal.co"
const stagingFractalWebserver = "https://staging-server.fractal.co"
const prodFractalWebserver = "https://prod-server.fractal.co"

const fractalWebserverAuthEndpoint = "/host_service/auth"
const fractalWebserverHeartbeatEndpoint = "/host_service/heartbeat"

// We define custom types to easily interact with the relevant webserver
// endpoints.

// We send the webserver the webserver the host instance ID when handshaking.
type handshakeRequest struct {
	InstanceID   string
	InstanceType string // EC2 instance type
	Region       string // AWS region
	AWSAmiID     string // AWS AMI ID
}

// We receive an auth token from the webserver if handshaking succeeded, which
// is necessary for communicating with it thereafter.
type handshakeResponse struct {
	AuthToken string
}

// We periodically send heartbeats to the webserver to notify it of this EC2 host's state
type heartbeatRequest struct {
	AuthToken        string // Handshake-response auth token to authenticate with webserver
	Timestamp        string // Current timestamp
	HeartbeatNumber  uint64 // Index of heartbeat since host service started
	InstanceID       string // EC2 instance ID
	TotalRAMinKB     string // Total amount of RAM on the host, in kilobytes
	FreeRAMinKB      string // Lower bound on RAM available on the host (not consumed by running containers), in kilobytes
	AvailRAMinKB     string // Upper bound on RAM available on the host (not consumed by running containers), in kilobytes
	IsDyingHeartbeat bool   // Whether this heartbeat is sent by the host service during its death
}

// GetFractalWebserver returns the appropriate webserver URL based on whether
// we're running in production, staging or development. Since
// `GetAppEnvironment()` caches its result, we don't need to cache this.
func GetFractalWebserver() string {
	switch GetAppEnvironment() {
	case EnvStaging:
		return stagingFractalWebserver
	case EnvProd:
		return prodFractalWebserver
	case EnvDev:
		return devFractalWebserver
	case EnvLocalDev:
		return localdevFractalWebserver
	default:
		return localdevFractalWebserver
	}
}

var authToken string
var numBeats uint64 = 0
var heartbeatHTTPClient = http.Client{
	Timeout: 30 * time.Second,
}

// As long as this channel is blocking, we should keep sending "alive"
// heartbeats. As soon as the channel is closed, we should send a "dying"
// heartbeat and no longer send any more heartbeats.
var heartbeatKeepAlive = make(chan interface{}, 1)

// initializeHeartbeat starts the heartbeat goroutine
func initializeHeartbeat() error {
	if GetAppEnvironment() == EnvLocalDev {
		Infof("Skipping initializing webserver heartbeats since running in LocalDev environment.")
		return nil
	}

	Infof("Initializing webserver heartbeats, communicating with webserver at %s", GetFractalWebserver())

	resp, err := handshake()
	if err != nil {
		return MakeError("Error handshaking with webserver: %v", err)
	}

	authToken = resp.AuthToken

	// Note that this goroutine does not listen to the global context, and is not
	// tracked by the global goroutineTracker. This is because we want the
	// heartbeat goroutine to outlive the global context, and all other
	// goroutines. Therefore, we have the explicit function `stopHeartbeats()`,
	// which ends up finishing off the heartbeat goroutine.
	go heartbeatGoroutine()

	return nil
}

// Instead of running exactly every minute, we choose a random time in the
// range [55, 65] seconds to prevent waves of hosts repeatedly crowding the
// webserver. Note also that we don't have to do any error handling here
// because sendHeartbeat() does not return or panic.
func heartbeatGoroutine() {
	defer Infof("Finished heartbeat goroutine.")
	timerChan := make(chan interface{})

	// Send initial heartbeat right away
	sendHeartbeat(false)

	for {
		sleepTime := 65000 - rand.Intn(10001)
		timer := time.AfterFunc(time.Duration(sleepTime)*time.Millisecond, func() { timerChan <- nil })

		select {
		case _, _ = <-heartbeatKeepAlive:
			// If we hit this case, that means that `heartbeatKeepAlive` was either
			// closed or written to (it should not be written to), but either way,
			// it's time to die.
			sendHeartbeat(true)

			// Stop timer to avoid leaking a goroutine (not that it matters if we're
			// shutting down, but still).
			if !timer.Stop() {
				<-timer.C
			}
			return

		case _ = <-timerChan:
			// There's just no time to die
			sendHeartbeat(false)
		}
	}
}

func stopHeartbeats() {
	close(heartbeatKeepAlive)
}

// Talk to the auth endpoint for the host service startup (to authenticate all
// future requests). We currently send the EC2 instance ID in the request as a
// (weak) layer of defense against unauthenticated/spoofed handshakes. We
// expect back a JSON response containing a field called "AuthToken".
func handshake() (handshakeResponse, error) {
	var resp handshakeResponse

	instanceID, err := GetAwsInstanceID()
	if err != nil {
		return resp, MakeError("handshake(): Couldn't get AWS instanceID. Error: %v", err)
	}

	instanceType, err := GetAwsInstanceType()
	if err != nil {
		return resp, MakeError("handshake(): Couldn't get AWS instanceType. Error: %v", err)
	}

	region, err := GetAwsPlacementRegion()
	if err != nil {
		return resp, MakeError("handshake(): Couldn't get AWS placementRegion. Error: %v", err)
	}

	amiID, err := GetAwsAmiID()
	if err != nil {
		return resp, MakeError("handshake(): Couldn't get AWS amiID. Error: %v", err)
	}

	requestURL := GetFractalWebserver() + fractalWebserverAuthEndpoint
	requestBody, err := json.Marshal(handshakeRequest{
		InstanceID:   instanceID,
		InstanceType: instanceType,
		Region:       region,
		AWSAmiID:     amiID,
	})
	if err != nil {
		return resp, MakeError("handshake(): Could not marshal the handshakeRequest object. Error: %v", err)
	}

	Infof("handshake(): Sending a POST request with body %s to URL %s", requestBody, requestURL)
	httpResp, err := heartbeatHTTPClient.Post(requestURL, "application/json", bytes.NewReader(requestBody))
	if err != nil {
		return resp, MakeError("handshake(): Got back an error from the webserver at URL %s. Error:  %v", requestURL, err)
	}

	// We would normally just read in body, err := iotuil.ReadAll(httpResp.body),
	// but the handshake is not properly implmented yet, and we need to avoid a
	// linter warning for an "ineffectual assingment to body", so we declare it
	// and only set it later.
	var body []byte
	body, err = ioutil.ReadAll(httpResp.Body)
	if err != nil {
		return resp, MakeError("handshake():: Unable to read body of response from webserver. Error: %v", err)
	}

	Infof("handshake(): got response code: %v", httpResp.StatusCode)
	Infof("handshake(): got response: %s", body)

	err = json.Unmarshal(body, &resp)
	if err != nil {
		return resp, MakeError("handshake():: Unable to unmarshal JSON response from the webserver!. Response: %s Error: %s", body, err)
	}
	return resp, nil
}

// We send a heartbeat. Note that the heartbeat number 0 is the initialization
// message of the host service to the webserver.  This function intentionally
// does not panic or return an error/value. Instead, we let the webserver deal
// with malformed heartbeats by potentially choosing to mark the instance as
// draining. This also simplifies our logging/error handling since we can just
// ignore errors in the heartbeat goroutine.
func sendHeartbeat(isDying bool) {
	// Prepare the body

	// We ignore errors in these function calls because errors will just get
	// passed on as an empty string or nil. It's not worth terminating the
	// instance over a malformed heartbeat --- we can let the webserver decide if
	// we want to mark the instance as draining.
	instanceID, _ := GetAwsInstanceID()
	totalRAM, _ := GetTotalMemoryInKB()
	freeRAM, _ := GetFreeMemoryInKB()
	availRAM, _ := GetAvailableMemoryInKB()

	requestURL := GetFractalWebserver() + fractalWebserverHeartbeatEndpoint
	requestBody, err := json.Marshal(heartbeatRequest{
		AuthToken:        authToken,
		Timestamp:        Sprintf("%s", time.Now()),
		HeartbeatNumber:  numBeats,
		InstanceID:       instanceID,
		TotalRAMinKB:     totalRAM,
		FreeRAMinKB:      freeRAM,
		AvailRAMinKB:     availRAM,
		IsDyingHeartbeat: isDying,
	})
	if err != nil {
		Errorf("Couldn't marshal requestBody into JSON. Error: %v", err)
	}

	Infof("Sending a heartbeat with body %s to URL %s", requestBody, requestURL)
	_, err = heartbeatHTTPClient.Post(requestURL, "application/json", bytes.NewReader(requestBody))
	if err != nil {
		Errorf("Error sending heartbeat: %s", err)
	}

	numBeats++
}
