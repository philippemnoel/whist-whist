package httputils

import (
	mandelboxtypes "github.com/whisthq/whist/backend/services/types"
)

// Request types

// MandelboxInfoRequest defines the (unauthenticated) `json_transport`
// endpoint.
type MandelboxInfoRequest struct {
	MandelboxID   mandelboxtypes.MandelboxID // MandelboxID, used for the json transport request map
	KioskMode     bool                       // Enable or disable kiosk mode in chromium
	LoadExtension bool                       // If the Whist extension should be loaded
	LocalClient   bool                       // Indicates if the request comes from a local client
	ResultChan    chan RequestResult         // Channel to pass the request result between goroutines
}

// MandelboxInfoRequestResult defines the data returned by the
// `json_transport` endpoint.
type MandelboxInfoRequestResult struct {
	HostPortForTCP32261 uint16 `json:"port_32261"`
	HostPortForTCP32262 uint16 `json:"port_32262"`
	HostPortForUDP32263 uint16 `json:"port_32263"`
	HostPortForTCP32273 uint16 `json:"port_32273"`
	AesKey              string `json:"aes_key"`
}

// ReturnResult is called to pass the result of a request back to the HTTP
// request handler.
func (s *MandelboxInfoRequest) ReturnResult(result interface{}, err error) {
	s.ResultChan <- RequestResult{
		Result: result,
		Err:    err,
	}
}

// createResultChan is called to create the Go channel to pass the request
// result back to the HTTP request handler via ReturnResult.
func (s *MandelboxInfoRequest) CreateResultChan() {
	if s.ResultChan == nil {
		s.ResultChan = make(chan RequestResult)
	}
}

// Mandelbox assign request
type MandelboxAssignRequest struct {
	Regions    []string `json:"regions"`
	CommitHash string   `json:"client_commit_hash"`
	UserEmail  string   `json:"user_email"`
	Version    string   `json:"version"`
	// The userID is obtained from the access token, but when testing it can be sent in the request
	UserID mandelboxtypes.UserID `json:"user_id"`
	// Channel to pass the request result between goroutines
	ResultChan chan RequestResult
}

type MandelboxAssignRequestResult struct {
	IP          string                     `json:"ip"`
	MandelboxID mandelboxtypes.MandelboxID `json:"mandelbox_id"`
	Error       string                     `json:"error"`
}

// ReturnResult is called to pass the result of a request back to the HTTP
// request handler.
func (s *MandelboxAssignRequest) ReturnResult(result interface{}, err error) {
	s.ResultChan <- RequestResult{
		Result: result,
		Err:    err,
	}
}

// createResultChan is called to create the Go channel to pass the request
// result back to the HTTP request handler via ReturnResult.
func (s *MandelboxAssignRequest) CreateResultChan() {
	if s.ResultChan == nil {
		s.ResultChan = make(chan RequestResult)
	}
}
