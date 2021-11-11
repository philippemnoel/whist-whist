package main

import (
	"context"
	"encoding/json"
	"io"
	"net/http"
	"os/exec"
	"sync"
	"time"

	"github.com/fractal/fractal/host-service/auth"
	logger "github.com/fractal/fractal/host-service/fractallogger"
	"github.com/fractal/fractal/host-service/mandelbox/portbindings"
	mandelboxtypes "github.com/fractal/fractal/host-service/mandelbox/types"
	"github.com/fractal/fractal/host-service/metadata"
	"github.com/fractal/fractal/host-service/utils"
	"github.com/golang-jwt/jwt/v4"
)

// Constants for use in setting up the HTTPS server
const (
	// Our HTTPS server listens on the port "HOST" converted to a telephone
	// number.
	PortToListen   uint16 = 4678
	certPath       string = utils.FractalPrivateDir + "cert.pem"
	privatekeyPath string = utils.FractalPrivateDir + "key.pem"
)

func init() {
	portbindings.Reserve(PortToListen, portbindings.TransportProtocolTCP)
}

// A ServerRequest represents a request from the server --- it is exported so
// that we can implement the top-level event handlers in parent packages. They
// simply return the result and any error message via ReturnResult.
type ServerRequest interface {
	ReturnResult(result interface{}, err error)
	createResultChan()
}

// A requestResult represents the result of a request that was successfully
// authenticated, parsed, and processed by the consumer.
type requestResult struct {
	Result interface{} `json:"-"`
	Err    error       `json:"error"`
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
				Result interface{} `json:"result"`
				Error  string      `json:"error"`
			}{r.Result, r.Err.Error()},
		)
	} else {
		// Send a 200 code
		status = http.StatusOK
		buf, err = json.Marshal(
			struct {
				Result interface{} `json:"result"`
			}{r.Result},
		)
	}

	w.WriteHeader(status)
	if err != nil {
		logger.Errorf("Error marshalling a %v HTTP Response body: %s", status, err)
	}
	_, _ = w.Write(buf)
}

// JSONTransportRequest defines the (unauthenticated) `json_transport`
// endpoint.
type JSONTransportRequest struct {
	AppName               mandelboxtypes.AppName               `json:"app_name,omitempty"`      // The app name to spin up (used when running in localdev, but in deployment the app name is set to browsers/chrome).
	ConfigEncryptionToken mandelboxtypes.ConfigEncryptionToken `json:"config_encryption_token"` // User-specific private encryption token
	JwtAccessToken        string                               `json:"jwt_access_token"`        // User's JWT access token
	MandelboxID           mandelboxtypes.MandelboxID           `json:"mandelbox_id"`            // MandelboxID, used for the json transport request map
	FirstRun              bool                                 `json:"first_run"`               // Flag indicating whether we should treat this as a first run in terms of the user's config
	JSONData              string                               `json:"json_data"`               // Arbitrary stringified JSON data to pass to mandelbox
	resultChan            chan requestResult                   // Channel to pass the request result between goroutines
}

// JSONTransportRequestResult defines the data returned by the
// `json_transport` endpoint.
type JSONTransportRequestResult struct {
	HostPortForTCP32262 uint16 `json:"port_32262"`
	HostPortForUDP32263 uint16 `json:"port_32263"`
	HostPortForTCP32273 uint16 `json:"port_32273"`
	AesKey              string `json:"aes_key"`
}

// ReturnResult is called to pass the result of a request back to the HTTP
// request handler.
func (s *JSONTransportRequest) ReturnResult(result interface{}, err error) {
	s.resultChan <- requestResult{result, err}
}

// createResultChan is called to create the Go channel to pass the request
// result back to the HTTP request handler via ReturnResult.
func (s *JSONTransportRequest) createResultChan() {
	if s.resultChan == nil {
		s.resultChan = make(chan requestResult)
	}
}

// processJSONDataRequest processes an HTTP request to receive data
// directly from the client app. It is handled in host-service.go
func processJSONDataRequest(w http.ResponseWriter, r *http.Request, queue chan<- ServerRequest) {
	// Verify that it is an PUT request
	if verifyRequestType(w, r, http.MethodPut) != nil {
		return
	}

	// Verify authorization and unmarshal into the right object type
	var reqdata JSONTransportRequest
	if err := authenticateAndParseRequest(w, r, &reqdata, !metadata.IsLocalEnv()); err != nil {
		logger.Errorf("Error authenticating and parsing %T: %s", reqdata, err)
		return
	}

	// Send request to queue, then wait for result
	queue <- &reqdata
	res := <-reqdata.resultChan

	res.send(w)
}

// handleJSONTransportRequest handles any incoming JSON transport requests. First it validates the JWT, then it verifies if
// the json data for the particular user exists, and finally it sends the data through the transport request map.
func handleJSONTransportRequest(serverevent ServerRequest, transportRequestMap map[mandelboxtypes.MandelboxID]chan *JSONTransportRequest, transportMapLock *sync.Mutex) {
	// Receive the value of the `json_transport request`
	req := serverevent.(*JSONTransportRequest)

	// First we validate the JWT received from the `json_transport` endpoint
	// Set up auth
	claims := new(auth.FractalClaims)
	parser := &jwt.Parser{SkipClaimsValidation: true}

	// Only verify auth in non-local environments
	if !metadata.IsLocalEnv() {
		// Decode the access token without validating any of its claims or
		// verifying its signature because we've already done that in
		// `authenticateAndParseRequest`. All we want to know is the value of the
		// sub (subject) claim.
		if _, _, err := parser.ParseUnverified(string(req.JwtAccessToken), claims); err != nil {
			logger.Errorf("There was a problem while parsing the access token for the second time: %s", err)
			return
		}
	}

	// If the jwt was valid, the next step is to verify if we have already received the JSONData from
	// this specific user. In case we have, we ignore the request. Otherwise we add the request to the map.
	// By performing this validation, we make the host service resistant to DDOS attacks, and by keeping track
	// of each user's json data, we avoid any race condition that might corrupt other mandelboxes.

	// Acquire lock on transport requests map
	transportMapLock.Lock()
	defer transportMapLock.Unlock()

	if transportRequestMap[req.MandelboxID] == nil {
		transportRequestMap[req.MandelboxID] = make(chan *JSONTransportRequest, 1)
	}

	// Send the JSONTransportRequest data through the map's channel and close the channel to
	// prevent more requests from being sent, this ensures we only receive the
	// json transport request once per user.
	transportRequestMap[req.MandelboxID] <- req
	close(transportRequestMap[req.MandelboxID])
}

// getJSONTransportRequestChannel returns the JSON transport request for the solicited user
// in a safe way. It also creates the channel in case it doesn't exists for that particular user.
func getJSONTransportRequestChannel(mandelboxID mandelboxtypes.MandelboxID,
	transportRequestMap map[mandelboxtypes.MandelboxID]chan *JSONTransportRequest, transportMapLock *sync.Mutex) chan *JSONTransportRequest {
	// Acquire lock on transport requests map
	transportMapLock.Lock()
	defer transportMapLock.Unlock()

	req := transportRequestMap[mandelboxID]

	if req == nil {
		transportRequestMap[mandelboxID] = make(chan *JSONTransportRequest, 1)
	}

	return transportRequestMap[mandelboxID]
}

func getAppName(mandelboxID mandelboxtypes.MandelboxID, transportRequestMap map[mandelboxtypes.MandelboxID]chan *JSONTransportRequest,
	transportMapLock *sync.Mutex) (*JSONTransportRequest, mandelboxtypes.AppName) {

	var AppName mandelboxtypes.AppName
	var req *JSONTransportRequest

	if metadata.IsLocalEnvWithoutDB() {
		// Receive the json transport request immediately when running on local env
		jsonchan := getJSONTransportRequestChannel(mandelboxID, transportRequestMap, transportMapLock)
		req = <-jsonchan
		if req.AppName == "" {
			// If no app name is set, we default to using the `browsers/chrome` image.
			AppName = mandelboxtypes.AppName("browsers/chrome")
		} else {
			AppName = req.AppName
		}
	} else {
		// If not on a local environment, we default to using the `browsers/chrome` image.
		AppName = mandelboxtypes.AppName("browsers/chrome")
	}
	return req, AppName
}

// Helper functions

// Function to verify the type (method) of a request
func verifyRequestType(w http.ResponseWriter, r *http.Request, method string) error {
	if r.Method != method {
		err := utils.MakeError("Received a request on %s to URL %s of type %s, but it should have been type %s", r.Host, r.URL, r.Method, method)
		logger.Error(err)

		http.Error(w, utils.Sprintf("Bad request type. Expected %s, got %s", method, r.Method), http.StatusBadRequest)

		return err
	}
	return nil
}

// Function to get the body of a result, authenticate it, and unmarshal it into
// a golang struct. Splitting this into a separate function has the following
// advantages:
// 1. We factor out duplicated functionality among all the endpoints that need
// authentication.
// 2. Doing so allows us not to unmarshal the `jwt_access_token` field, and
// therefore prevents needing to pass it to client code in other packages that
// don't need to understand our authentication mechanism or read the secret
// key. We could alternatively do this by creating two separate types of
// structs containing the data required for the endpoint --- one without the
// auth secret, and one with it --- but this requires duplication of struct
// definitions and writing code to copy values from one struct to another.
// 3. If we get a bad, unauthenticated request we can minimize the amount of
// processsing power we devote to it. This is useful for being resistant to
// Denial-of-Service attacks.
// Note: authenticateAndParseRequest is declared as a variable to allow
// it to be mocked for testing.
var authenticateAndParseRequest = func(w http.ResponseWriter, r *http.Request, s ServerRequest, authorizeAsBackend bool) (err error) {
	// Get body of request
	body, err := io.ReadAll(r.Body)
	if err != nil {
		http.Error(w, "Malformed body", http.StatusBadRequest)
		return utils.MakeError("Error getting body from request on %s to URL %s: %s", r.Host, r.URL, err)
	}

	// Extract only the jwt_access_token field from a raw JSON unmarshalling that
	// delays as much decoding as possible
	var rawmap map[string]*json.RawMessage
	err = json.Unmarshal(body, &rawmap)
	if err != nil {
		http.Error(w, "Malformed body", http.StatusBadRequest)
		return utils.MakeError("Error raw-unmarshalling JSON body sent on %s to URL %s: %s", r.Host, r.URL, err)
	}

	if authorizeAsBackend {
		var requestAuthSecret string

		err = func() error {
			if value, ok := rawmap["jwt_access_token"]; ok {
				return json.Unmarshal(*value, &requestAuthSecret)
			}
			return utils.MakeError("Request body had no \"jwt_access_token\" field.")
		}()

		if err != nil {
			http.Error(w, "Unauthorized", http.StatusUnauthorized)
			return utils.MakeError("Error getting jwt_access_token from JSON body sent on %s to URL %s: %s", r.Host, r.URL, err)
		}

		// Actually verify authentication. We check that the access token sent is
		// a valid JWT signed by Auth0.
		_, err := auth.Verify(requestAuthSecret)

		if err != nil {
			http.Error(w, "Unauthorized", http.StatusUnauthorized)
			return utils.MakeError("Received an unpermissioned backend request on %s to URL %s. Error: %s", r.Host, r.URL, err)
		}
	}

	// Now, actually do the unmarshalling into the right object type
	err = json.Unmarshal(body, s)
	if err != nil {
		http.Error(w, "Malformed body", http.StatusBadRequest)
		return utils.MakeError("Could not fully unmarshal the body of a request sent on %s to URL %s: %s", r.Host, r.URL, err)
	}

	// Set up the result channel
	s.createResultChan()

	return nil
}

// StartHTTPServer returns a channel of events from the webserver as its first return value
func StartHTTPServer(globalCtx context.Context, globalCancel context.CancelFunc, goroutineTracker *sync.WaitGroup) (<-chan ServerRequest, error) {
	logger.Info("Setting up HTTP server.")

	err := initializeTLS()
	if err != nil {
		return nil, utils.MakeError("Error starting HTTP Server: %v", err)
	}

	// Buffer up to 100 events so we don't block. This should be mostly
	// unnecessary, but we want to be able to handle a burst without blocking.
	events := make(chan ServerRequest, 100)

	createHandler := func(f func(http.ResponseWriter, *http.Request, chan<- ServerRequest)) func(http.ResponseWriter, *http.Request) {
		return func(w http.ResponseWriter, r *http.Request) {
			f(w, r, events)
		}
	}

	// Create a custom HTTP Request Multiplexer
	mux := http.NewServeMux()
	mux.Handle("/", http.NotFoundHandler())
	mux.HandleFunc("/json_transport", createHandler(processJSONDataRequest))

	// Create the server itself
	server := &http.Server{
		Addr:    utils.Sprintf("0.0.0.0:%v", PortToListen),
		Handler: mux,
	}

	// Start goroutine that shuts down `server` if the global context gets
	// cancelled.
	goroutineTracker.Add(1)
	go func() {
		defer goroutineTracker.Done()

		// Start goroutine that actually listens for requests
		goroutineTracker.Add(1)
		go func() {
			defer goroutineTracker.Done()

			if err := server.ListenAndServeTLS(certPath, privatekeyPath); err != nil && err.Error() != "http: Server closed" {
				close(events)
				logger.Panicf(globalCancel, "Error listening and serving in httpserver: %s", err)
			}
		}()

		// Listen for global context cancellation
		<-globalCtx.Done()

		// This is only necessary since we don't have the ability to subscribe to
		// database events. In particular, the webserver might mark this host
		// service as draining after allocating a mandelbox on it. If we didn't
		// have this sleep, we would stop accepting requests right away, and the
		// SpinUpMandelbox request from the client app would error out. We don't
		// want that, so we accept requests for another 30 seconds. This would be
		// annoying in local development, so it's disabled in that case.
		// TODO: get rid of this once we have pubsub
		if !metadata.IsLocalEnv() {
			logger.Infof("Global context cancelled. Starting 30 second grace period for requests before http server is shutdown...")
			time.Sleep(30 * time.Second)
		}

		logger.Infof("Shutting down httpserver...")
		shutdownCtx, shutdownCancel := context.WithTimeout(globalCtx, 30*time.Second)
		defer shutdownCancel()

		err := server.Shutdown(shutdownCtx)
		if err != nil {
			logger.Infof("Shut down httpserver with error %s", err)
		} else {
			logger.Info("Gracefully shut down httpserver.")
		}
	}()

	return events, nil
}

// Creates a TLS certificate/private key pair for secure communication with the Whist webserver
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
		"-addext", "subjectAltName=IP:127.0.0.1",
		"-keyout",
		privatekeyPath,
		"-out",
		certPath,
	)
	output, err := cmd.CombinedOutput()
	if err != nil {
		return utils.MakeError("Unable to create x509 private key/certificate pair. Error: %v, Command output: %s", err, output)
	}

	logger.Info("Successfully created TLS certificate/private key pair. Certificate path: %s", certPath)
	return nil
}
