// Copyright (c) 2021-2022 Whist Technologies, Inc.

/*
This file contains the code related to starting the scaling service's HTTP server,
and functions for handling the different types of requests. The implementation is
simple, but it has features that make it more robust in case of misuse or attacks
(such as rate limiting, read and write time outs).

The request handlers are responsible for authentication, verification, parsing, etc.
and perform minimal processing of the data. After doing so they send the event through
the server's channel so the event handler can send it to the scaling algorithms in turn.
Some of the logic and types are found on the `httputils` package, which is shared between
scaling service and host service.
*/

package main

import (
	"crypto/tls"
	"encoding/json"
	"io"
	"net/http"
	"os"
	"time"

	"github.com/google/uuid"
	"github.com/whisthq/whist/backend/services/host-service/auth"
	"github.com/whisthq/whist/backend/services/httputils"
	"github.com/whisthq/whist/backend/services/metadata"
	"github.com/whisthq/whist/backend/services/scaling-service/payments"
	algos "github.com/whisthq/whist/backend/services/scaling-service/scaling_algorithms/default"
	"github.com/whisthq/whist/backend/services/subscriptions"
	"github.com/whisthq/whist/backend/services/types"
	"github.com/whisthq/whist/backend/services/utils"
	logger "github.com/whisthq/whist/backend/services/whistlogger"
	"golang.org/x/time/rate"
)

// The port the host service is listening on. This will be used for
// redirecting JSON transport requests to the instance.
const HostServicePort uint16 = 4678

// mandelboxAssignHandler is the http handler for any requests to the `/assign` endpoint. It authenticates
// the request, verifies the type, and parses the body. After that it sends it to the server events channel.
func mandelboxAssignHandler(w http.ResponseWriter, r *http.Request, events chan<- algos.ScalingEvent) {
	// Verify that we got a POST request
	err := verifyRequestType(w, r, http.MethodPost)
	if err != nil {
		logger.Errorf("error verifying request type: %s", err)
		http.Error(w, http.StatusText(http.StatusBadRequest), http.StatusBadRequest)
		return
	}

	// Extract access token from request header
	accessToken, err := httputils.GetAccessToken(r)
	if err != nil {
		logger.Error(err)
		http.Error(w, "Did not receive an access token", http.StatusUnauthorized)
		return
	}

	var reqdata httputils.MandelboxAssignRequest
	claims, err := httputils.AuthenticateRequest(w, r, &reqdata)
	if err != nil {
		logger.Error(err)
		return
	}

	if !metadata.IsLocalEnv() {
		// Add user id to the request. This way we don't expose the
		// access token to other processes that don't need access to it.
		reqdata.UserID = types.UserID(claims.Subject)
	}

	// Once we have authenticated and validated the request send it to the scaling
	// algorithm for processing. Mandelbox assign is region-agnostic so we don't need
	// to specify a region on the event. The event handler will select the default region automatically.
	events <- algos.ScalingEvent{
		ID:   uuid.NewString(),
		Type: "SERVER_MANDELBOX_ASSIGN_EVENT",
		Data: &reqdata,
	}
	res := <-reqdata.ResultChan
	assignResult := res.Result.(httputils.MandelboxAssignRequestResult)

	var (
		buf           []byte
		status        int
		mandelboxInfo httputils.MandelboxInfoRequestResult
	)

	// Send a request to the host service running on the assigned instance to get the
	// information of the mandelbox. If the response is successful, return the mandelbox
	// ports and private key to the frontend as part of the assign request result.
	if assignResult.Error != "" {
		// Send a 503
		status = http.StatusServiceUnavailable
	} else {
		mandelboxInfo, err = getMandelboxInfo(assignResult.IP, assignResult.MandelboxID.String(), accessToken)
		if err != nil {
			logger.Errorf("failed to get mandelbox info from host service: %s", err)
			status = http.StatusServiceUnavailable
		} else {
			status = http.StatusOK
		}
	}

	buf, err = json.Marshal(mandelboxInfo)
	w.WriteHeader(status)
	if err != nil {
		logger.Errorf("error marshalling a %d HTTP Response body: %s", status, err)
	}
	_, _ = w.Write(buf)
}

// paymentSessionHandler handles a payment session request. It verifies the access token,
// and then creates a StripeClient to get the URL of a payment session.
func paymentSessionHandler(w http.ResponseWriter, r *http.Request) {
	// Verify that we got a GET request
	err := verifyRequestType(w, r, http.MethodGet)
	if err != nil {
		// err is already logged
		http.Error(w, "", http.StatusMethodNotAllowed)
		return
	}

	// Extract access token from request header
	accessToken, err := httputils.GetAccessToken(r)
	if err != nil {
		logger.Error(err)
		http.Error(w, "Did not receive an access token", http.StatusUnauthorized)
		return
	}

	// Get claims from access token
	claims, err := auth.ParseToken(accessToken)
	if err != nil {
		logger.Errorf("received an unpermissioned backend request on %s to URL %s: %s", r.Host, r.URL, err)
		http.Error(w, "Invalid access token", http.StatusUnauthorized)
		return
	}

	// Setup a config database GraphQL client to get the Stripe configurations.
	// The client is simply passed to the payments client, no database operations
	// are done here.
	useConfigDB := true

	configGraphqlClient := &subscriptions.GraphQLClient{}
	err = configGraphqlClient.Initialize(useConfigDB)
	if err != nil {
		logger.Errorf("failed to setup config GraphQL client: %s", err)
		http.Error(w, "", http.StatusInternalServerError)
	}

	// Create a new StripeClient to handle the customer
	paymentsClient := &payments.PaymentsClient{}
	stripeClient := &payments.StripeClient{}
	err = paymentsClient.Initialize(claims.CustomerID, claims.SubscriptionStatus, configGraphqlClient, stripeClient)
	if err != nil {
		logger.Errorf("failed to Initialize Stripe Client: %s", err)
		http.Error(w, "", http.StatusInternalServerError)
		return
	}

	sessionUrl, err := paymentsClient.CreateSession()
	if err != nil {
		logger.Errorf("failed to create Stripe Session: %s", err)
		http.Error(w, "", http.StatusInternalServerError)
		return
	}

	// Marshal and send session URL
	buf, err := json.Marshal(map[string]string{
		"url": sessionUrl,
	})
	if err != nil {
		logger.Errorf("error marshalling HTTP Response body: %s", err)
		http.Error(w, "", http.StatusInternalServerError)
		return
	}

	w.WriteHeader(http.StatusOK)
	_, _ = w.Write(buf)
}

// getMandelboxInfo processes an HTTP request to receive data
// directly from the frontend.
func getMandelboxInfo(ip string, mandelboxID string, accessToken string) (httputils.MandelboxInfoRequestResult, error) {
	var mandelboxInfo httputils.MandelboxInfoRequestResult

	// Send the request to the instance and then return the response
	url := utils.Sprintf("https://%s:%v/mandelbox/%s", ip, HostServicePort, mandelboxID)

	// Create a new request
	hostReq, err := http.NewRequest("GET", url, nil)
	if err != nil {
		return mandelboxInfo, err
	}

	// Add necessary headers to host request
	hostReq.Header.Add("Content-Type", "application/json")
	hostReq.Header.Add("Authorization", utils.Sprintf("Bearer %s", accessToken))

	// Instanciate a new http client with a custom transport
	tr := &http.Transport{
		TLSClientConfig: &tls.Config{
			InsecureSkipVerify: true,
		},
	}
	client := &http.Client{Transport: tr}

	// Now that request is fully assembled and the client is initialized, send the request
	res, err := client.Do(hostReq)
	if err != nil {
		logger.Error(err)
		return mandelboxInfo, err
	}
	defer res.Body.Close()

	body, err := io.ReadAll(res.Body)
	if err != nil {
		return mandelboxInfo, err
	}

	// Unmarshal and parse the response to return a result object
	var info struct {
		Result httputils.MandelboxInfoRequestResult `json:"result"`
	}
	err = json.Unmarshal(body, &info)
	if err != nil {
		return mandelboxInfo, err
	}

	mandelboxInfo = info.Result
	return mandelboxInfo, nil
}

// throttleMiddleware will limit requests on the endpoint using the provided rate limiter.
// It uses a token bucket algorithm, so that every interval of time the "bucket" will refill
// and continue to serve tokens up to a maximum defined by the burst capacity. In case the
// limit is exceeded, return a http 429 error (too many requests).
func throttleMiddleware(limiter *rate.Limiter, f func(http.ResponseWriter, *http.Request)) func(http.ResponseWriter, *http.Request) {
	return http.HandlerFunc(func(rw http.ResponseWriter, r *http.Request) {
		if !limiter.Allow() {
			http.Error(rw, http.StatusText(http.StatusTooManyRequests), http.StatusTooManyRequests)
		}
		f(rw, r)
	})
}

// verifyPaymentMiddleware will verify if the user has an active subscription in Stripe. This
// middleware should be used to restrict endpoint for paying users only.
func VerifyPaymentMiddleware(f func(http.ResponseWriter, *http.Request)) http.Handler {
	return http.HandlerFunc(func(rw http.ResponseWriter, r *http.Request) {
		// Get and parse Authorization header from access token
		accessToken, err := httputils.GetAccessToken(r)
		if err != nil {
			logger.Error(err)
			http.Error(rw, http.StatusText(http.StatusUnauthorized), http.StatusUnauthorized)
			return
		}

		// Verify if the user's subscription is valid
		paymentValid, err := payments.VerifyPayment(accessToken)
		if err != nil {
			logger.Errorf("failed to validate Stripe payment from access token: %s", err)
			http.Error(rw, http.StatusText(http.StatusInternalServerError), http.StatusInternalServerError)
			return
		}

		if !paymentValid {
			logger.Warningf("User does not have an active Stripe subscription.")
			http.Error(rw, http.StatusText(http.StatusForbidden), http.StatusForbidden)
			return
		}
		f(rw, r)
	})
}

// Function to verify the type (method) of a request
func verifyRequestType(w http.ResponseWriter, r *http.Request, method string) error {
	if r == nil {
		err := utils.MakeError("received a nil request expecting to be type %s", method)
		logger.Error(err)

		http.Error(w, utils.Sprintf("Bad request. Expected %s, got nil", method), http.StatusBadRequest)

		return err
	}

	if r.Method != method {
		err := utils.MakeError("received a request on %s to URL %s of type %s, but it should have been type %s", r.Host, r.URL, r.Method, method)
		logger.Error(err)

		http.Error(w, utils.Sprintf("Bad request type. Expected %s, got %s", method, r.Method), http.StatusBadRequest)

		return err
	}
	return nil
}

// StartHTTPServer will start the server with the necessary configurations and
// assign the handlers to each endpoint.
func StartHTTPServer(events chan algos.ScalingEvent) {
	logger.Infof("Starting HTTP server...")

	createHandler := func(f func(http.ResponseWriter, *http.Request, chan<- algos.ScalingEvent)) func(http.ResponseWriter, *http.Request) {
		return func(w http.ResponseWriter, r *http.Request) {
			f(w, r, events)
		}
	}

	// Start a new rate limiter. This will limit requests on an endpoint
	// to every `interval` with a burst of up to `burst` requests. This
	// will help mitigate Denial of Service attacks, or a rogue frontend.
	interval := 1 * time.Second
	burst := 10
	limiter := rate.NewLimiter(rate.Every(interval), burst)

	// Create the final handlers, with the necessary middleware
	assignHandler := throttleMiddleware(limiter, httputils.EnableCORS(createHandler(mandelboxAssignHandler)))
	paymentsHandler := httputils.EnableCORS(paymentSessionHandler)

	// Create a custom HTTP Request Multiplexer
	mux := http.NewServeMux()
	mux.Handle("/", http.NotFoundHandler())
	mux.Handle("/mandelbox/assign", http.HandlerFunc(assignHandler))
	mux.Handle("/payment_portal_url", http.HandlerFunc(paymentsHandler))

	// The PORT env var will be automatically set by Heroku.
	// If running on localdev, use default port.
	port := os.Getenv("PORT")
	if metadata.IsLocalEnv() || port == "" {
		port = "7730"
	}

	// Set read/write timeouts to help mitigate potential rogue clients
	// or DDOS attacks.
	srv := &http.Server{
		Addr:         utils.Sprintf("0.0.0.0:%v", port),
		ReadTimeout:  10 * time.Second,
		WriteTimeout: 10 * time.Second,
		IdleTimeout:  30 * time.Second,
		Handler:      mux,
	}

	go func() {
		logger.Error(srv.ListenAndServe())
	}()
}
