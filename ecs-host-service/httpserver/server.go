package httpserver

import (
	"crypto/subtle"
	"encoding/json"
	"io/ioutil"
	"net/http"
	"os/exec"

	logger "github.com/fractal/fractal/ecs-host-service/fractallogger"
)

// Variables for the auth_secret used to communicate between the webserver and
// host service --- filled in by linker
var webserverAuthSecretDev, webserverAuthSecretStaging, webserverAuthSecretProd string

var webserverAuthSecret string

// Constants for use in setting up the HTTPS server
const (
	// We listen on the port "HOST" converted to a telephone number
	portToListen       = ":4678"
	FractalPrivatePath = "/fractalprivate/"
	certPath           = FractalPrivatePath + "cert.pem"
	privatekeyPath     = FractalPrivatePath + "key.pem"
)

// A ServerRequest represents a request from the server --- it is exported so
// that we can implement the top-level event handlers in parent packages. They
// simply return the result and any error message via ReturnResult.
type ServerRequest interface {
	ReturnResult(result string, err error)
	createResultChan()
}

// A requestResult represents the result of a request that was successfully
// authenticated, parsed, and processed by the consumer.
type requestResult struct {
	Result string `json:"-"`
	Err    error  `json:"error"`
}

// send is called to send an HTTP response
func (r requestResult) send(w http.ResponseWriter) {
	var buf []byte
	var err error
	var status int

	if r.Err != nil {
		// Send a 406
		status = http.StatusNotAcceptable
		buf, err = json.Marshal(
			struct {
				Result string `json:"result"`
				Error  string `json:"error"`
			}{r.Result, r.Err.Error()},
		)
	} else {
		// Send a 200 code
		status = http.StatusOK
		buf, err = json.Marshal(
			struct {
				Result string `json:"result"`
			}{r.Result},
		)
	}

	w.WriteHeader(status)
	if err != nil {
		logger.Errorf("Error marshalling a %v HTTP Response body: %s", status, err)
	}
	_, _ = w.Write(buf)
}

// MountCloudStorageRequest defines the (unauthenticated) mount_cloud_storage
// endpoint
type MountCloudStorageRequest struct {
	HostPort     int                `json:"host_port"`     // Port on the host to mount the cloud storage drive to
	Provider     string             `json:"provider"`      // Cloud storage provider (i.e. google_drive)
	AccessToken  string             `json:"access_token"`  // Cloud storage access token
	RefreshToken string             `json:"refresh_token"` // Cloud storage access token refresher
	Expiry       string             `json:"expiry"`        // Expiration date of access_token
	TokenType    string             `json:"token_type"`    // Type of access_token, currently always "bearer"
	ClientID     string             `json:"client_id"`     // ID to tell cloud storage provider that we are the Fractal client they approved
	ClientSecret string             `json:"client_secret"` // Secret associated with client_id
	resultChan   chan requestResult // Channel to pass cloud storage mounting result between goroutines
}

// ReturnResult is called to pass the result of a request back to the HTTP
// request handler
func (s *MountCloudStorageRequest) ReturnResult(result string, err error) {
	s.resultChan <- requestResult{result, err}
}

// createResultChan is called to create the Go channel to pass cloud storage mounting request
// result back to the HTTP request handler via ReturnResult
func (s *MountCloudStorageRequest) createResultChan() {
	if s.resultChan == nil {
		s.resultChan = make(chan requestResult)
	}
}

// Process an HTTP request for mounting a cloud storage folder to a container, to be handled in ecs-host-service.go
func processMountCloudStorageRequest(w http.ResponseWriter, r *http.Request, queue chan<- ServerRequest) {
	// Verify that it is a POST
	if verifyRequestType(w, r, http.MethodPost) != nil {
		return
	}

	// Verify authorization and unmarshal into the right object type
	var reqdata MountCloudStorageRequest
	if err := authenticateAndParseRequest(w, r, &reqdata); err != nil {
		logger.Infof(err.Error())
		return
	}

	// Send request to queue, then wait for result
	queue <- &reqdata
	res := <-reqdata.resultChan

	res.send(w)
}

// SetContainerDPIRequest defines the (unauthenticated) DPI endpoint
type SetContainerDPIRequest struct {
	HostPort   int                `json:"host_port"` // Port on the host to mount the DPI file to
	DPI        int                `json:"dpi"`       // DPI to set for the container
	resultChan chan requestResult // Channel to pass DPI setting result between goroutines
}

// ReturnResult is called to pass the result of a request back to the HTTP
// request handler
func (s *SetContainerDPIRequest) ReturnResult(result string, err error) {
	s.resultChan <- requestResult{result, err}
}

// createResultChan is called to create the Go channel to pass DPI setting request
// result back to the HTTP request handler via ReturnResult
func (s *SetContainerDPIRequest) createResultChan() {
	if s.resultChan == nil {
		s.resultChan = make(chan requestResult)
	}
}

// Process an HTTP request for setting the DPI of a container, to be handled in ecs-host-service.go
func processSetContainerDPIRequest(w http.ResponseWriter, r *http.Request, queue chan<- ServerRequest) {
	// Verify that it is an PUT request
	if verifyRequestType(w, r, http.MethodPut) != nil {
		return
	}

	// Verify authorization and unmarshal into the right object type
	var reqdata SetContainerDPIRequest
	if err := authenticateAndParseRequest(w, r, &reqdata); err != nil {
		logger.Infof(err.Error())
		return
	}

	// Send request to queue, then wait for result
	queue <- &reqdata
	res := <-reqdata.resultChan

	res.send(w)
}

// Function to verify the type (method) of a request
func verifyRequestType(w http.ResponseWriter, r *http.Request, method string) error {
	if r.Method != method {
		err := logger.MakeError("Received a request from %s to URL %s of type %s, but it should have been type %s", r.Host, r.URL, r.Method, method)
		logger.Error(err)

		http.Error(w, logger.Sprintf("Bad request type. Expected %s, got %s", method, r.Method), http.StatusBadRequest)

		return err
	}
	return nil
}

// Function to get the body of a result, authenticate it, and unmarshal it into
// a golang struct. Splitting this into a separate function has the following
// advantages:
// 1. We factor out duplicated functionality among all the endpoints that need
// authentication.
// 2. Doing so allows us not to unmarshal the `auth_secret` field, and
// therefore prevents needing to pass it to client code in other packages that
// don't need to understand our authentication mechanism or read the secret
// key. We could alternatively do this by creating two separate types of
// structs containing the data required for the endpoint --- one without the
// auth secret, and one with it --- but this requires duplication of struct
// definitions and writing code to copy values from one struct to another.
// 3. If we get a bad, unauthenticated request we can minimize the amount of
// processsing power we devote to it. This is useful for being resistant to
// Denial-of-Service attacks.
func authenticateAndParseRequest(w http.ResponseWriter, r *http.Request, s ServerRequest) (err error) {
	// Get body of request
	body, err := ioutil.ReadAll(r.Body)
	if err != nil {
		http.Error(w, "Malformed body", http.StatusBadRequest)
		return logger.MakeError("Error getting body from request from %s to URL %s: %s", r.Host, r.URL, err)
	}

	// Extract only the auth_secret field from a raw JSON unmarshalling that
	// delays as much decoding as possible
	var rawmap map[string]*json.RawMessage
	err = json.Unmarshal(body, &rawmap)
	if err != nil {
		http.Error(w, "Malformed body", http.StatusBadRequest)
		return logger.MakeError("Error raw-unmarshalling JSON body sent from %s to URL %s: %s", r.Host, r.URL, err)
	}
	var requestAuthSecret string
	err = func() error {
		if value, ok := rawmap["auth_secret"]; ok {
			return json.Unmarshal(*value, &requestAuthSecret)
		} else {
			return logger.MakeError("Request body had no \"auth_secret\" field.")
		}
	}()
	if err != nil {
		http.Error(w, "Unauthorized", http.StatusUnauthorized)
		return logger.MakeError("Error getting auth_secret from JSON body sent from %s to URL %s: %s", r.Host, r.URL, err)
	}

	// Actually verify authentication. Note that we check the length of the token
	// that is sent before comparing the strings. This leaks the length of the
	// correct key (which does not help the attacker too much), but provides us
	// the benefit of preventing a super-long request from clogging up our host
	// service with huge memory allocations.
	if len(webserverAuthSecret) != len(requestAuthSecret) ||
		subtle.ConstantTimeCompare(
			[]byte(webserverAuthSecret),
			[]byte(requestAuthSecret),
		) == 0 {
		http.Error(w, "Unauthorized", http.StatusUnauthorized)
		return logger.MakeError("Received a bad auth_secret from %s to URL %s", r.Host, r.URL)
	}

	// Now, actually do the unmarshalling into the right object type
	err = json.Unmarshal(body, s)
	if err != nil {
		http.Error(w, "Malformed body", http.StatusBadRequest)
		return logger.MakeError("Could not fully unmarshal the body of a request sent from %s to URL %s: %s", r.Host, r.URL, err)
	}

	// Set up the result channel
	s.createResultChan()

	return nil
}

// StartHTTPSServer returns a channel of events from the webserver as its first return value
func StartHTTPSServer() (<-chan ServerRequest, error) {
	logger.Info("Setting up HTTP server.")

	// Select the correct environment (dev, staging, prod)
	// TODO: account for localdev here
	switch logger.GetAppEnvironment() {
	case logger.EnvLocalDev:
		fallthrough
	case logger.EnvDev:
		webserverAuthSecret = webserverAuthSecretDev
	case logger.EnvStaging:
		webserverAuthSecret = webserverAuthSecretStaging
	case logger.EnvProd:
		webserverAuthSecret = webserverAuthSecretProd
	}

	err := initializeTLS()
	if err != nil {
		return nil, logger.MakeError("Error starting HTTP Server: %v", err)
	}

	// Buffer up to 100 events so we don't block. This should be mostly
	// unnecessary, but we want to be able to handle a burst without blocking.
	events := make(chan ServerRequest, 100)

	createHandler := func(f func(http.ResponseWriter, *http.Request, chan<- ServerRequest)) func(http.ResponseWriter, *http.Request) {
		return func(w http.ResponseWriter, r *http.Request) {
			f(w, r, events)
		}
	}

	http.Handle("/", http.NotFoundHandler())
	http.HandleFunc("/mount_cloud_storage", createHandler(processMountCloudStorageRequest))
	http.HandleFunc("/set_container_dpi", createHandler(processSetContainerDPIRequest))
	go func() {
		// TODO: defer things correctly so that a panic here is actually caught and resolved
		logger.Panicf("HTTP Server Error: %v", http.ListenAndServeTLS("0.0.0.0"+portToListen, certPath, privatekeyPath, nil))
	}()

	return events, nil
}

// Creates a TLS certificate/private key pair for secure communication with the Fractal webserver
func initializeTLS() error {
	// Create a self-signed passwordless certificate
	// https://unix.stackexchange.com/questions/104171/create-ssl-certificate-non-interactively
	cmd := exec.Command(
		"/usr/bin/openssl",
		"req",
		"-new",
		"-newkey",
		"rsa:4096",
		"-days",
		"365",
		"-nodes",
		"-x509",
		"-subj",
		"/C=US/ST=./L=./O=./CN=.",
		"-keyout",
		privatekeyPath,
		"-out",
		certPath,
	)
	output, err := cmd.CombinedOutput()
	if err != nil {
		return logger.MakeError("Unable to create x509 private key/certificate pair. Error: %v, Command output: %s", err, output)
	}

	logger.Info("Successfully created TLS certificate/private key pair.")
	return nil
}
