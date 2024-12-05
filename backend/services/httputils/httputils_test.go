package httputils

import (
	"net/http"
	"net/http/httptest"
	"os"
	"reflect"
	"strings"
	"testing"
)

func TestGetAccessToken(t *testing.T) {
	var tests = []struct {
		name, header, expected string
		err                    bool
	}{
		{"Valid Authorization header", "Bearer test_valid_token", "test_valid_token", false},
		{"Malformed Authorization header", "test_malformed_token", "", true},
		{"Empty Authorization header", "", "", true},
		{"Undefined Authorization header", "undefined", "", true},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			r := httptest.NewRequest(http.MethodPost, "https://localhost", nil)
			r.Header.Add("Authorization", tt.header)
			token, err := GetAccessToken(r)
			if err != nil && !tt.err {
				t.Errorf("did not expect error, got: %s", err)
			}

			if token != tt.expected {
				t.Errorf("expected token to be %s, got %s", tt.expected, token)
			}
		})
	}
}

func TestParseRequest(t *testing.T) {
	var tests = []struct {
		name     string
		request  ServerRequest
		jsonBody string
		expected ServerRequest
	}{
		{"Valid assign request", &MandelboxAssignRequest{}, `{
			"regions": [
				"us-east-1"
			],
			"client_commit_hash": "test_sha",
			"user_email": "user@whist.com",
			"version": "1.0.0",
			"session_id": 1234567890
		}`, &MandelboxAssignRequest{
			Regions: []string{
				"us-east-1",
			},
			CommitHash: "test_sha",
			UserEmail:  "user@whist.com",
			Version:    "1.0.0",
		}},
		{"Empty assign request", &MandelboxAssignRequest{}, `{}`, &MandelboxAssignRequest{}},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			body := strings.NewReader(tt.jsonBody)
			w := httptest.NewRecorder()
			r := httptest.NewRequest(http.MethodPost, "https://localhost", body)

			_, err := ParseRequest(w, r, tt.request)
			if err != nil {
				t.Errorf("Did not expect error, got: %s", err)
			}

			if ok := reflect.DeepEqual(reflect.TypeOf(tt.request).Elem(), reflect.TypeOf(tt.expected).Elem()); !ok {
				t.Errorf("Expected request to be parsed to %v, got %v", tt.expected, tt.request)
			}
		})
	}
}

func TestVerifyRequestType(t *testing.T) {
	var tests = []struct {
		name, method string
	}{
		{"GET Request", http.MethodGet},
		{"POST Request", http.MethodPost},
		{"PUT Request", http.MethodPut},
	}

	methodsToTest := []string{
		http.MethodHead,
		http.MethodOptions,
		http.MethodGet,
		http.MethodPost,
		http.MethodPut,
		http.MethodDelete,
		http.MethodPatch,
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			for _, method := range methodsToTest {
				w := httptest.NewRecorder()
				r := httptest.NewRequest(method, "https://localhost", nil)

				err := VerifyRequestType(w, r, tt.method)
				if err != nil && tt.method == method {
					t.Errorf("Did not expect error, got: %s", err)
				}
			}
		})
	}
}

func TestEnableCORS(t *testing.T) {
	corsHandler := EnableCORS(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		w.Write([]byte(""))
	})

	srv := httptest.NewServer(http.HandlerFunc(corsHandler))

	resp, err := http.Get(srv.URL)
	if err != nil {
		t.Errorf("did not expect error, got: %s", err)
	}

	wantHeaders := map[string]string{
		"Access-Control-Allow-Origin":  "*",
		"Access-Control-Allow-Headers": "Origin Accept Content-Type X-Requested-With",
		"Access-Control-Allow-Methods": "POST PUT OPTIONS",
	}

	// Check that all CORS headers were added to the response
	for k, v := range wantHeaders {
		header := resp.Header.Get(k)
		if header != v {
			t.Errorf("header %v was not added to request", k)
		}
	}
}

func TestInitializeTLS(t *testing.T) {
	var tests = []struct {
		name              string
		keyPath, certPath string
		err               bool
	}{
		{"Valid key and cert path", "./key.pem", "./cert.pem", false},
		{"Invalid key and cert path", ".", ".", true},
		{"Empty key and cert path", "", "", true},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := InitializeTLS(tt.keyPath, tt.certPath)
			if err != nil && !tt.err {
				t.Errorf("Did not expect error, got: %s", err)
			} else if tt.err {
				return
			}

			_, err = os.Stat(tt.keyPath)
			if err != nil {
				t.Errorf("Failed to create keypath: %s", err)
			}
			_, err = os.Stat(tt.certPath)
			if err != nil {
				t.Errorf("Failed to create certPath: %s", err)
			}
		})
	}
}
