package heroku

import (
	"os"
	"strings"
	"testing"

	"github.com/whisthq/whist/host-service/metadata"
	"github.com/whisthq/whist/host-service/utils"
)

func TestGetAppName(t *testing.T) {
	var webserverTests = []struct {
		environmentVar string
		want           string
	}{
		{"LOCALDEV", "whist-dev-server"},
		{"LOCALDEVWITHDB", "whist-dev-server"},
		{"DEV", "whist-dev-server"},
		{"STAGING", "whist-staging-server"},
		{"PROD", "whist-prod-server"},
		{"UNKNOWN", "whist-dev-server"},
	}

	for _, tt := range webserverTests {
		testname := utils.Sprintf("%s,%v", tt.environmentVar, tt.want)
		t.Run(testname, func(t *testing.T) {

			// Set the CI environment variable to the test environment
			os.Setenv("APP_ENV", tt.environmentVar)
			got := GetAppName()

			if got != tt.want {
				t.Errorf("got %v, want %v", got, tt.want)
			}
		})
	}
}

func TestGetHasuraName(t *testing.T) {
	var hasuraTests = []struct {
		environmentVar string
		want           string
	}{
		{"LOCALDEV", "whist-dev-hasura"},
		{"LOCALDEVWITHDB", "whist-dev-hasura"},
		{"DEV", "whist-dev-hasura"},
		{"STAGING", "whist-staging-hasura"},
		{"PROD", "whist-prod-hasura"},
		{"UNKNOWN", "whist-dev-hasura"},
	}

	for _, tt := range hasuraTests {
		testname := utils.Sprintf("%s,%v", tt.environmentVar, tt.want)
		t.Run(testname, func(t *testing.T) {

			// Set the CI environment variable to the test environment
			os.Setenv("APP_ENV", tt.environmentVar)
			got := GetHasuraName()

			if got != tt.want {
				t.Errorf("got %v, want %v", got, tt.want)
			}
		})
	}
}

func TestMain(m *testing.M) {
	// Set the GetAppEnvironment function before starting any tests.
	mockGetAppEnvironment()
	os.Exit(m.Run())
}

// mockGetAppEnvironment is used to mock the `GetAppEnvironment` function
// without the memoization. Doing this allows us to tests on all app environments.
func mockGetAppEnvironment() {
	metadata.GetAppEnvironment = func() metadata.AppEnvironment {
		env := strings.ToLower(os.Getenv("APP_ENV"))
		switch env {
		case "development", "dev":
			return metadata.EnvDev
		case "staging":
			return metadata.EnvStaging
		case "production", "prod":
			return metadata.EnvProd
		case "localdevwithdb", "localdev_with_db", "localdev_with_database":
			return metadata.EnvLocalDevWithDB
		default:
			return metadata.EnvLocalDev
		}
	}
}
