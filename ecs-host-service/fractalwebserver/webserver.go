package fractalwebserver

import (
	"bytes"
	"encoding/json"
	"io/ioutil"
	"math/rand"
	"net/http"
	"time"

	// We use this package instead of the standard library log so that we never
	// forget to send a message via Sentry. For the same reason, we make sure
	// not to import the fmt package either, instead separating required
	// functionality in this imported package as well.
	logger "github.com/fractal/fractal/ecs-host-service/fractallogger"
)

// Fractal webserver URLs and relevant webserver endpoints
const devHost = "https://dev-server.fractal.co"
const stagingHost = "https://staging-server.fractal.co"
const productionHost = "https://prod-server.fractal.co"

const authEndpoint = "/host_service/auth"
const heartbeatEndpoint = "/host_service/heartbeat"

// We cache the webserver host so we can avoid the overhead of repeatedly
// checking the APP_ENV environment variable (though that could be cached as
// well).
var webserverHost = getWebserverHost()

// We define custom types to easily interact with the relevant webserver endpoints

// We send the webserver the webserver the host instance ID when handshaking
type handshakeRequest struct {
	InstanceID string
}

// We receive an auth token from the webserver if handshaking succeeded, which is
// necessary for communicating with it thereafter
type handshakeResponse struct {
	AuthToken string
}

// We periodically send heartbeats to the webserver to notify it of this EC2 host's state
type heartbeatRequest struct {
	AuthToken        string // Handshake-response auth token to authenticate with webserver
	Timestamp        string // Current timestamp
	HeartbeatNumber  uint64 // Index of heartbeat since host service started
	InstanceID       string // EC2 instance ID
	InstanceType     string // EC2 instance type
	TotalRAMinKB     string // Total amount of RAM on the host, in kilobytes
	FreeRAMinKB      string // Lower bound on RAM available on the host (not consumed by running containers), in kilobytes
	AvailRAMinKB     string // Upper bound on RAM available on the host (not consumed by running containers), in kilobytes
	IsDyingHeartbeat bool   // Whether this heartbeat is sent by the host service during its death
}

var authToken string
var numBeats uint64 = 0
var httpClient = http.Client{
	Timeout: 10 * time.Second,
}

// Get the appropriate webserverHost based on whether we're running in
// production, staging or development
func getWebserverHost() string {
	switch logger.GetAppEnvironment() {
	case logger.EnvStaging:
		logger.Infof("Running in staging, communicating with %s", stagingHost)
		return stagingHost
	case logger.EnvProd:
		logger.Infof("Running in production, communicating with %s", productionHost)
		return productionHost
	case logger.EnvDev:
		fallthrough
	default:
		logger.Infof("Running in development, communicating with %s", devHost)
		return devHost
	}
}

// InitializeHeartbeat starts the heartbeat goroutine
func InitializeHeartbeat() error {
	if logger.GetAppEnvironment() == logger.EnvLocalDev {
		logger.Infof("Skipping initializing webserver heartbeats since running in LocalDev environment.")
	} else {
		logger.Infof("Initializing webserver heartbeats.")
	}

	resp, err := handshake()
	if err != nil {
		return logger.MakeError("Error handshaking with webserver: %v", err)
	}

	authToken = resp.AuthToken

	go heartbeatGoroutine()

	return nil
}

// Instead of running exactly every minute, we choose a random time in the
// range [55, 65] seconds to prevent waves of hosts repeatedly crowding the
// webserver. Note also that we don't have to do any error handling here
// because sendHeartbeat() does not return or panic.
func heartbeatGoroutine() {
	for {
		sendHeartbeat(false)
		sleepTime := 65000 - rand.Intn(10001)
		time.Sleep(time.Duration(sleepTime) * time.Millisecond)
	}
}

// SendGracefulShutdownNotice sends a heartbeat with IsDyingHeartbeat set to true
func SendGracefulShutdownNotice() {
	sendHeartbeat(true)
}

// Talk to the auth endpoint for the host service startup (to authenticate all
// future requests). We currently send the EC2 instance ID in the request as a
// (weak) layer of defense against unauthenticated/spoofed handshakes. We
// expect back a JSON response containing a field called "AuthToken".
func handshake() (handshakeResponse, error) {
	var resp handshakeResponse

	instanceID, err := logger.GetAwsInstanceID()
	if err != nil {
		return resp, logger.MakeError("handshake(): Couldn't get AWS instanceID. Error: %v", err)
	}

	requestURL := webserverHost + authEndpoint
	requestBody, err := json.Marshal(handshakeRequest{instanceID})
	if err != nil {
		return resp, logger.MakeError("handshake(): Could not marshal the handshakeRequest object. Error: %v", err)
	}

	logger.Infof("handshake(): Sending a POST request with body %s to URL %s", requestBody, requestURL)
	httpResp, err := httpClient.Post(requestURL, "application/json", bytes.NewReader(requestBody))
	if err != nil {
		return resp, logger.MakeError("handshake(): Got back an error from the webserver at URL %s. Error:  %v", requestURL, err)
	}

	// We would normally just read in body, err := iotuil.ReadAll(httpResp.body),
	// but the handshake is not properly implmented yet, and we need to avoid a
	// linter warning for an "ineffectual assingment to body", so we declare it
	// and only set it later.
	var body []byte
	_, err = ioutil.ReadAll(httpResp.Body)
	if err != nil {
		return resp, logger.MakeError("handshake():: Unable to read body of response from webserver. Error: %v", err)
	}
	// Overwrite body because handshake is not implemented properly yet
	body, _ = json.Marshal(handshakeResponse{"testauthtoken"})

	logger.Infof("handshake(): got response code: %v", httpResp.StatusCode)
	logger.Infof("handshake(): got response: %s", body)

	err = json.Unmarshal(body, &resp)
	if err != nil {
		return resp, logger.MakeError("handshake():: Unable to unmarshal JSON response from the webserver!. Response: %s Error: %s", body, err)
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
	instanceID, _ := logger.GetAwsInstanceID()
	instanceType, _ := logger.GetAwsInstanceType()
	totalRAM, _ := logger.GetTotalMemoryInKB()
	freeRAM, _ := logger.GetFreeMemoryInKB()
	availRAM, _ := logger.GetAvailableMemoryInKB()

	requestURL := webserverHost + heartbeatEndpoint
	requestBody, err := json.Marshal(heartbeatRequest{
		AuthToken:        authToken,
		Timestamp:        logger.Sprintf("%s", time.Now()),
		HeartbeatNumber:  numBeats,
		InstanceID:       instanceID,
		InstanceType:     instanceType,
		TotalRAMinKB:     totalRAM,
		FreeRAMinKB:      freeRAM,
		AvailRAMinKB:     availRAM,
		IsDyingHeartbeat: isDying,
	})
	if err != nil {
		logger.Errorf("Couldn't marshal requestBody into JSON. Error: %v", err)
	}

	logger.Infof("Sending a heartbeat with body %s to URL %s", requestBody, requestURL)
	_, err = httpClient.Post(requestURL, "application/json", bytes.NewReader(requestBody))
	if err != nil {
		logger.Errorf("Error sending heartbeat: %s", err)
	}

	numBeats++
}
