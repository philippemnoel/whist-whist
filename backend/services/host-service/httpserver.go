package main

import (
	"context"
	"net/http"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/google/uuid"
	"github.com/whisthq/whist/backend/services/host-service/auth"
	"github.com/whisthq/whist/backend/services/host-service/mandelbox/portbindings"
	"github.com/whisthq/whist/backend/services/host-service/metrics"
	"github.com/whisthq/whist/backend/services/httputils"
	"github.com/whisthq/whist/backend/services/metadata"
	"github.com/whisthq/whist/backend/services/types"
	"github.com/whisthq/whist/backend/services/utils"
	logger "github.com/whisthq/whist/backend/services/whistlogger"
)

// Constants for use in setting up the HTTPS server
const (
	// Our HTTPS server listens on the port "HOST" converted to a telephone
	// number.
	PortToListen   uint16 = 4678
	certPath       string = utils.WhistPrivateDir + "cert.pem"
	privatekeyPath string = utils.WhistPrivateDir + "key.pem"
)

func init() {
	portbindings.Reserve(PortToListen, portbindings.TransportProtocolTCP)
}

// mandelboxInfoHandler processes an HTTP request to receive data
// directly from the Whist frontend. It is handled in host-service.go
func mandelboxInfoHandler(w http.ResponseWriter, r *http.Request, queue chan<- httputils.ServerRequest) {
	// Start timer to measure average time processing http requests.
	start := time.Now()

	// Verify that it is a GET request
	if httputils.VerifyRequestType(w, r, http.MethodGet) != nil {
		metrics.Increment("FailedRequests")
		return
	}

	if !metadata.IsLocalEnv() {
		// Extract access token from request header
		accessToken, err := httputils.GetAccessToken(r)
		if err != nil {
			logger.Error(err)
			http.Error(w, "Did not receive an access token", http.StatusUnauthorized)
			return
		}

		// Validate access token
		_, err = auth.ParseToken(accessToken)
		if err != nil {
			logger.Errorf("received an unpermissioned backend request on %s to URL %s: %s", r.Host, r.URL, err)
			http.Error(w, "Invalid access token", http.StatusUnauthorized)
			return
		}
	}

	_, mandelboxIDString, _ := strings.Cut(r.URL.Path, "/mandelbox/")
	if mandelboxIDString == "" {
		logger.Errorf("mandelbox id not present or malformed request path: %s", r.URL.Path)
		http.Error(w, "Malformed path", http.StatusBadRequest)
		return
	}

	mandelboxID, err := uuid.Parse(mandelboxIDString)
	if err != nil {
		logger.Errorf("malformed mandelbox id: %s", err)
		http.Error(w, "Mandelbox id has an incorrect format", http.StatusBadRequest)
		return
	}

	var kioskMode, loadExtension, localClient bool

	// Parse the query parameters for localdev. Since these values only really affect
	// local development, we ignore any errors parsing them.
	query := r.URL.Query()
	if len(query) != 0 {
		kioskMode, _ = strconv.ParseBool(query.Get("kiosk_mode"))
		loadExtension, _ = strconv.ParseBool(query.Get("load_extension"))
		localClient, _ = strconv.ParseBool(query.Get("local_client"))
	}

	reqdata := httputils.MandelboxInfoRequest{
		MandelboxID:   types.MandelboxID(mandelboxID),
		KioskMode:     kioskMode,
		LoadExtension: loadExtension,
		LocalClient:   localClient,
		ResultChan:    make(chan httputils.RequestResult),
	}

	// Send request to queue, then wait for result
	queue <- &reqdata
	res := <-reqdata.ResultChan

	res.Send(w)

	// Measure elapsed milliseconds and send to metrics.
	elapsed := time.Since(start)
	metrics.Increment("SuccessfulRequests")
	metrics.Add("TotalRequestTimeMS", elapsed.Milliseconds())
}

// StartHTTPServer returns a channel of events from the HTTP server as its first return value
func StartHTTPServer(globalCtx context.Context, globalCancel context.CancelFunc, goroutineTracker *sync.WaitGroup) (<-chan httputils.ServerRequest, error) {
	logger.Info("Setting up HTTP server.")

	err := httputils.InitializeTLS(privatekeyPath, certPath)
	if err != nil {
		return nil, utils.MakeError("Error starting HTTP Server: %v", err)
	}

	// Buffer up to 100 events so we don't block. This should be mostly
	// unnecessary, but we want to be able to handle a burst without blocking.
	events := make(chan httputils.ServerRequest, 100)

	createHandler := func(f func(http.ResponseWriter, *http.Request, chan<- httputils.ServerRequest)) func(http.ResponseWriter, *http.Request) {
		return func(w http.ResponseWriter, r *http.Request) {
			f(w, r, events)
		}
	}

	mandelboxInfo := httputils.EnableCORS(createHandler(mandelboxInfoHandler))
	// Create a custom HTTP Request Multiplexer
	mux := http.NewServeMux()
	mux.HandleFunc("/mandelbox/", mandelboxInfo)
	mux.Handle("/", http.NotFoundHandler())

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
