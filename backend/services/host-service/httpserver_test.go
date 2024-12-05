package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"net/http/httptest"
	"reflect"
	"testing"
	"testing/iotest"

	"github.com/whisthq/whist/backend/services/httputils"
	"github.com/whisthq/whist/backend/services/types"
	"github.com/whisthq/whist/backend/services/utils"
)

type MandelboxInfoResult struct {
	Result httputils.MandelboxInfoRequestResult `json:"result"`
}

// TestSpinUpHandler calls processSpinUpMandelboxRequest and checks to see if
// request data is successfully passed into the processing queue.
func TestSpinUpHandler(t *testing.T) {
	var tests = []struct {
		name     string
		r        httputils.MandelboxInfoRequest
		expected httputils.MandelboxInfoRequestResult
	}{
		{"Valid mandelbox info request", httputils.MandelboxInfoRequest{
			MandelboxID: types.MandelboxID(utils.PlaceholderTestUUID()),
			ResultChan:  make(chan httputils.RequestResult),
		}, httputils.MandelboxInfoRequestResult{
			HostPortForTCP32261: 32261,
			HostPortForTCP32262: 32262,
			HostPortForUDP32263: 32263,
			HostPortForTCP32273: 32273,
			AesKey:              "aes_key",
		}},
		{"Empty mandelbox info request", httputils.MandelboxInfoRequest{
			MandelboxID: types.MandelboxID{},
			ResultChan:  make(chan httputils.RequestResult),
		}, httputils.MandelboxInfoRequestResult{}},
	}

	q := make(chan httputils.ServerRequest, 5)
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		mandelboxInfoHandler(w, r, q)
	}))
	defer ts.Close()

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			req, err := generateTestMandelboxInfoRequest(ts.URL, tt.r)
			if err != nil {
				t.Fatalf("failed to generate test mandelbox info request: %s", err)
			}

			go func() {
				serverEvent := <-q
				t.Logf("Got server event request in queue: %v", serverEvent)
				serverEvent.ReturnResult(tt.expected, nil)
			}()

			// Make test request
			res, err := ts.Client().Do(req)
			if err != nil {
				t.Fatal(err)
			}

			body, err := io.ReadAll(res.Body)
			res.Body.Close()
			if err != nil {
				t.Fatal(err)
			}

			type MandelboxInfoResult struct {
				Info httputils.MandelboxInfoRequestResult `json:"result"`
			}

			var result MandelboxInfoResult
			err = json.Unmarshal(body, &result)
			if err != nil {
				t.Error(err)
			}

			if ok := reflect.DeepEqual(result.Info, tt.expected); !ok {
				t.Errorf("got %v, expected %v", result, tt.expected)
			}

		})
	}
}

// TestSendRequestResult tests if a valid request resolves successfully
func TestSendRequestResult(t *testing.T) {
	reqResult := httputils.RequestResult{
		Result: "test result",
	}
	res := httptest.NewRecorder()
	io.WriteString(res, "test content")

	// A 200 status code should be set
	reqResult.Send(res)

	if res.Result().StatusCode != http.StatusOK {
		t.Fatalf("error sending a valid request results. Expected status code %v, got %v", http.StatusOK, res.Result().StatusCode)
	}
}

// TestVerifyRequestWrongType will create a request method that does not match the expected method
func TestVerifyRequestWrongType(t *testing.T) {
	res := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodPost, "https://localhost", nil)

	// verifyRequestType will catch request with wrong method and return an error
	if err := httputils.VerifyRequestType(res, req, http.MethodPut); err == nil {
		t.Fatal("error verifying request type when the request method does not match. Expected an error, got nil")
	}

	if res.Result().StatusCode != http.StatusBadRequest {
		t.Fatalf("error verifying request type when the request method does not match. Expected status code %v, got %v", http.StatusBadRequest, res.Result().StatusCode)
	}
}

// TestVerifyRequestTypeNilRequest checks if nil requests are handled properly
func TestVerifyRequestTypeNilRequest(t *testing.T) {
	res := httptest.NewRecorder()

	// verifyRequestType will catch nil request and return an error
	if err := httputils.VerifyRequestType(res, nil, http.MethodPut); err == nil {
		t.Fatal("error verifying request type when request is nil. Expected an error, got nil")
	}

	if res.Result().StatusCode != http.StatusBadRequest {
		t.Fatalf("error verifying request type when request is nil. Expected status code %v, got %v", http.StatusBadRequest, res.Result().StatusCode)
	}
}

// TestAuthenticateAndParseRequestReadAllErr checks if body read errors are handled properly
func TestAuthenticateAndParseRequestReadAllErr(t *testing.T) {
	res := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodPut, "https://localhost", iotest.ErrReader(errors.New("test error")))
	req.Header.Add("Authorization", "Bearer test_token")

	testMandelboxInfoRequest := httputils.MandelboxInfoRequest{
		ResultChan: make(chan httputils.RequestResult),
	}

	// authenticateAndParseRequest will get an error trying to read request body and will cause an error
	_, err := httputils.AuthenticateRequest(res, req, &testMandelboxInfoRequest)

	if err == nil {
		t.Fatalf("error authenticating and parsing request when real all fails. Expected err, got nil")
	}

	if res.Result().StatusCode != http.StatusBadRequest {
		t.Fatalf("error authenticating and parsing request when real all fails. Expected status code %v, got %v", http.StatusBadRequest, res.Result().StatusCode)
	}
}

// TestAuthenticateAndParseRequestEmptyBody checks if an empty body will error successfully
func TestAuthenticateAndParseRequestEmptyBody(t *testing.T) {
	res := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodPut, "https://localhost", bytes.NewReader([]byte{}))
	req.Header.Add("Authorization", "Bearer test_token")

	testMandelboxInfoRequest := httputils.MandelboxInfoRequest{
		ResultChan: make(chan httputils.RequestResult),
	}

	// authenticateAndParseRequest will be unable to unmarshal an empty body and will cause an error
	_, err := httputils.AuthenticateRequest(res, req, &testMandelboxInfoRequest)

	if err == nil {
		t.Fatalf("error authenticating and parsing request with empty body. Expected err, got nil")
	}

	if res.Result().StatusCode != http.StatusBadRequest {
		t.Fatalf("error authenticating and parsing request with empty body. Expected status code %v, got %v", http.StatusBadRequest, res.Result().StatusCode)
	}
}

// generateTestMandelboxInfoRequest takes a request body and creates an
// HTTP GET request for /mandelbox/:id
func generateTestMandelboxInfoRequest(baseURL string, requestBody httputils.MandelboxInfoRequest) (*http.Request, error) {
	httpRequest, err := http.NewRequest(http.MethodGet, baseURL+"/mandelbox/"+requestBody.MandelboxID.String(), nil)
	if err != nil {
		return nil, utils.MakeError("error creating put request: %v", err)
	}

	httpRequest.Header.Add("Authorization", "Bearer test_token")
	return httpRequest, nil
}
