package scaling_algorithms

import (
	"context"
	"sync"
	"testing"
	"time"

	"github.com/google/uuid"
	"github.com/whisthq/whist/backend/services/httputils"
	"github.com/whisthq/whist/backend/services/metadata"
	"github.com/whisthq/whist/backend/services/subscriptions"
	"github.com/whisthq/whist/backend/services/types"
	"github.com/whisthq/whist/backend/services/utils"
)

var defaultRegions []string = []string{"us-east-1", "us-west-1"}

// TestMandelboxAssign test the happy path of assigning a mandelbox to a user.
func TestMandelboxAssign(t *testing.T) {
	context, cancel := context.WithCancel(context.Background())
	defer cancel()

	var tests = []struct {
		name             string
		env              metadata.AppEnvironment
		capacity         int64
		regions          []string
		clientSHA, want  string
		shouldBeAssigned bool
	}{
		{"happy path dev", metadata.EnvDev, instanceCapacity["g4dn.2xlarge"], defaultRegions, CLIENT_COMMIT_HASH_DEV_OVERRIDE, "", true},               // Happy path, sufficient capacity and matching commit hash
		{"commit hash mismatch dev", metadata.EnvDev, instanceCapacity["g4dn.2xlarge"], defaultRegions, "outdated-sha", "COMMIT_HASH_MISMATCH", false}, // Commit mismatch, sufficient capacity but different commit hashes
		{"no capacity dev", metadata.EnvDev, 0, defaultRegions, CLIENT_COMMIT_HASH_DEV_OVERRIDE, "NO_INSTANCE_AVAILABLE", false},                       // No capacity, but matching commit hash
		{"some unavailable regions dev", metadata.EnvDev, instanceCapacity["g4dn.2xlarge"], []string{
			"ap-southeast-1",
			"ap-northeast-1",
			"ap-south-1",
			"us-west-1",
			"us-east-1",
		}, CLIENT_COMMIT_HASH_DEV_OVERRIDE, "", true}, // Some unavailable regions
		{"only unavailable regions dev", metadata.EnvDev, instanceCapacity["g4dn.2xlarge"], []string{
			"ap-southeast-1",
			"ap-northeast-1",
			"ap-south-1",
			"eu-north-1",
		}, CLIENT_COMMIT_HASH_DEV_OVERRIDE, "", true}, // Only unavailable regions

		{"happy path staging", metadata.EnvStaging, instanceCapacity["g4dn.2xlarge"], defaultRegions, CLIENT_COMMIT_HASH_DEV_OVERRIDE, "", true},               // Happy path, sufficient capacity and matching commit hash
		{"commit hash mismatch staging", metadata.EnvStaging, instanceCapacity["g4dn.2xlarge"], defaultRegions, "outdated-sha", "COMMIT_HASH_MISMATCH", false}, // Commit mismatch, sufficient capacity but different commit hashes
		{"no capacity staging", metadata.EnvStaging, 0, defaultRegions, CLIENT_COMMIT_HASH_DEV_OVERRIDE, "NO_INSTANCE_AVAILABLE", false},                       // No capacity, but matching commit hash
		{"some unavailable regions staging", metadata.EnvStaging, instanceCapacity["g4dn.2xlarge"], []string{
			"ap-southeast-1",
			"ap-northeast-1",
			"ap-south-1",
			"us-west-1",
			"us-east-1",
		}, CLIENT_COMMIT_HASH_DEV_OVERRIDE, "", true}, // Some unavailable regions
		{"only unavailable regions staging", metadata.EnvStaging, instanceCapacity["g4dn.2xlarge"], []string{
			"ap-southeast-1",
			"ap-northeast-1",
			"ap-south-1",
			"eu-north-1",
		}, CLIENT_COMMIT_HASH_DEV_OVERRIDE, "", true}, // Only unavailable regions

		{"happy path prod", metadata.EnvProd, instanceCapacity["g4dn.2xlarge"], defaultRegions, CLIENT_COMMIT_HASH_DEV_OVERRIDE, "", true},               // Happy path, sufficient capacity and matching commit hash
		{"commit hash mismatch prod", metadata.EnvProd, instanceCapacity["g4dn.2xlarge"], defaultRegions, "outdated-sha", "COMMIT_HASH_MISMATCH", false}, // Commit mismatch, sufficient capacity but different commit hashes
		{"no capacity prod", metadata.EnvProd, 0, defaultRegions, CLIENT_COMMIT_HASH_DEV_OVERRIDE, "NO_INSTANCE_AVAILABLE", false},                       // No capacity, but matching commit hash
		{"some unavailable regions prod", metadata.EnvProd, instanceCapacity["g4dn.2xlarge"], []string{
			"ap-southeast-1",
			"ap-northeast-1",
			"ap-south-1",
			"us-west-1",
			"us-east-1",
		}, CLIENT_COMMIT_HASH_DEV_OVERRIDE, "", true}, // Some unavailable regions
		{"only unavailable regions prod", metadata.EnvProd, instanceCapacity["g4dn.2xlarge"], []string{
			"ap-southeast-1",
			"ap-northeast-1",
			"ap-south-1",
			"eu-north-1",
		}, CLIENT_COMMIT_HASH_DEV_OVERRIDE, "", true}, // Only unavailable regions
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {

			// Override environment so we can test commit hashes on the request
			metadata.GetAppEnvironment = func() metadata.AppEnvironment {
				return tt.env
			}

			// Populate test instances that will be used when
			// mocking database functions.
			testInstances = []subscriptions.Instance{
				{
					ID:                "test-assign-instance-1",
					Provider:          "AWS",
					ImageID:           "test-image-id",
					Status:            "ACTIVE",
					Type:              "g4dn.2xlarge",
					Region:            "us-east-1",
					IPAddress:         "1.1.1.1/24",
					ClientSHA:         "test-sha",
					RemainingCapacity: int64(tt.capacity),
				},
				{
					ID:                "test-assign-instance-2",
					Provider:          "AWS",
					ImageID:           "test-image-id",
					Status:            "ACTIVE",
					Type:              "g4dn.2xlarge",
					Region:            "us-west-1",
					IPAddress:         "1.1.1.1/24",
					ClientSHA:         "test-sha",
					RemainingCapacity: int64(tt.capacity),
				},
				{
					ID:                "test-assign-instance-3",
					Provider:          "AWS",
					ImageID:           "test-image-id",
					Status:            "ACTIVE",
					Type:              "g4dn.2xlarge",
					Region:            "ap-south-1",
					IPAddress:         "1.1.1.1/24",
					ClientSHA:         "test-sha",
					RemainingCapacity: int64(tt.capacity),
				},
			}

			// Set the current image for testing
			testImages = []subscriptions.Image{
				{
					Provider:  "AWS",
					Region:    "test-region",
					ImageID:   "test-image-id-old",
					ClientSHA: "test-sha",
					UpdatedAt: time.Date(2022, 04, 11, 11, 54, 30, 0, time.Local),
				},
			}

			testMandelboxes = []subscriptions.Mandelbox{
				{
					ID:         types.MandelboxID(uuid.New()),
					App:        "CHROME",
					InstanceID: "test-instance-id",
					UserID:     "test-user-id",
					SessionID:  utils.Sprintf("%v", time.Now().UnixMilli()),
					Status:     "WAITING",
					CreatedAt:  time.Now(),
					UpdatedAt:  time.Now(),
				},
			}

			testAssignRequest := &httputils.MandelboxAssignRequest{
				Regions:    tt.regions,
				CommitHash: tt.clientSHA,
				UserEmail:  "user@whist.com",
				Version:    "2.13.2",
			}
			testAssignRequest.CreateResultChan()

			wg := &sync.WaitGroup{}
			errorChan := make(chan error, 1)

			frontendVersion = &subscriptions.FrontendVersion{
				ID:    1,
				Major: 3,
				Minor: 0,
				Micro: 0,
			}

			wg.Add(1)
			go func() {
				defer wg.Done()

				err := testAlgorithm.MandelboxAssign(context, ScalingEvent{Data: testAssignRequest})
				errorChan <- err
			}()

			var assignResult httputils.MandelboxAssignRequestResult
			wg.Add(1)
			go func() {
				defer wg.Done()

				res := <-testAssignRequest.ResultChan
				assignResult = res.Result.(httputils.MandelboxAssignRequestResult)
			}()

			wg.Wait()

			// Print the errors from the assign action to verify the
			// behavior is the expected one.
			err := <-errorChan
			t.Log(err)
			if err != nil && tt.shouldBeAssigned {
				t.Errorf("error while testing mandelbox assign: %s", err)
			}

			if assignResult.Error != tt.want {
				t.Errorf("expected mandelbox assign request Error field to be %v, got %v", tt.want, assignResult.Error)
			}

			id, err := uuid.Parse(assignResult.MandelboxID.String())
			if err != nil {
				t.Errorf("got an invalid Mandelbox ID from the assign request %s: %s", id, err)
			}

			if tt.shouldBeAssigned && assignResult.IP != "1.1.1.1" {
				t.Errorf("none of the test instances were assigned correctly.")
			}
		})
	}

}
