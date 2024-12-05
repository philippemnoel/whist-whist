package main

import (
	"context"
	"os"
	"sync"
	"testing"
	"time"

	"github.com/docker/docker/api/types"
	"github.com/docker/docker/api/types/container"
	"github.com/docker/docker/api/types/network"
	"github.com/docker/docker/client"
	v1 "github.com/opencontainers/image-spec/specs-go/v1"
	"github.com/whisthq/whist/backend/services/host-service/mandelbox"
	mandelboxtypes "github.com/whisthq/whist/backend/services/types"
	"github.com/whisthq/whist/backend/services/utils"
	logger "github.com/whisthq/whist/backend/services/whistlogger"
)

// mockClient is a mock Docker client that implements the CommonAPIClient
// interface and can be used as the input to SpinUpMandelbox.
// We are embedding the CommonAPIClient interface inside this struct.
// This will allow us to selectively implement (mock) only the methods we need.
//
// See:
// https://eli.thegreenplace.net/2020/embedding-in-go-part-3-interfaces-in-structs/
type mockClient struct {
	client.CommonAPIClient

	config       container.Config
	hostConfig   container.HostConfig
	browserImage string

	started bool
	stopped bool
}

// ImageList mocks the ImageAPIClient interface's ImageList method and returns
// a single mock image with the tag "testApp"
func (m *mockClient) ImageList(ctx context.Context, options types.ImageListOptions) ([]types.ImageSummary, error) {
	logger.Infof("Called mock ImageList method.")
	testAppImage := types.ImageSummary{
		Containers: 1234,
		Created:    1234,
		ID:         "testImage",
		Labels: map[string]string{
			"test": "test",
		},
		ParentID:    "testParent",
		RepoDigests: []string{"testDigest"},
		RepoTags:    []string{m.browserImage},
		SharedSize:  1234,
		Size:        1234,
		VirtualSize: 1234,
	}
	logger.Infof("%v", options)
	images := []types.ImageSummary{testAppImage}
	return images, nil
}

// ContainerCreate mocks the ContainerAPIClient's ContainerCreate method, saves the inputs,
// and returns a successful container created response.
func (m *mockClient) ContainerCreate(ctx context.Context, config *container.Config, hostConfig *container.HostConfig, networkingConfig *network.NetworkingConfig, platform *v1.Platform, containerName string) (container.ContainerCreateCreatedBody, error) {
	logger.Infof("Called mock ContainerCreate method.")

	m.config = *config
	m.hostConfig = *hostConfig

	createSuccessful := container.ContainerCreateCreatedBody{
		ID:       "testContainer",
		Warnings: []string{},
	}
	return createSuccessful, nil
}

// ContainerStart mocks the ContainerAPIClient's ContainerStart method and returns
// nil, simulating successful container start. The method will sleep for 3 seconds
// to simulate (+ exaggerate) the behaviour of a real container start.
func (m *mockClient) ContainerStart(ctx context.Context, container string, options types.ContainerStartOptions) error {
	logger.Infof("Called mock ContainerStart method.")
	time.Sleep(3 * time.Second)
	m.started = true
	return nil
}

// ContainerStop mocks the ContainerAPIClient's ContainerStop method and returns
// nil, simulating successful container stopping. The method will sleep for 1 seconds
// to simulate (+ exaggerate) the behaviour of a real container stop.
func (m *mockClient) ContainerStop(ctx context.Context, id string, timeout *time.Duration) error {
	logger.Infof("Called mock ContainerStop method.")
	time.Sleep(1 * time.Second)
	m.stopped = true
	return nil
}

// ContainerRemove mocks the ContainerAPIClient's ContainerRemove method and returns
// nil, simulating successful container removal. The method will sleep for 1 seconds
// to simulate (+ exaggerate) the behaviour of a real container being removed.
func (m *mockClient) ContainerRemove(ctx context.Context, id string, options types.ContainerRemoveOptions) error {
	logger.Infof("Called mock ContainerRemove method.")
	time.Sleep(1 * time.Second)
	return nil
}

// TestCreateDockerClient will check if a valid docker client is created without error
func TestCreateDockerClient(t *testing.T) {
	dockerClient, err := createDockerClient()

	if err != nil {
		t.Fatalf("error creating docker client. Error: %v", err)
	}

	if dockerClient == nil {
		t.Fatal("error creating docker client. Expected client, got nil")
	}

	if _, ok := interface{}(dockerClient).(*client.Client); !ok {
		t.Fatalf("error creating docker client. Expected docker client")
	}
}

func TestMandelboxDieHandler(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	dockerClient := mockClient{
		browserImage: "whisthq",
	}

	tracker := &sync.WaitGroup{}

	mandelboxDieChan := make(chan bool, 10)
	m := mandelbox.New(ctx, tracker, mandelboxtypes.MandelboxID(utils.PlaceholderTestUUID()), mandelboxDieChan)
	m.RegisterCreation(mandelboxtypes.DockerID("test-docker-id"))

	mandelboxDieHandler("test-docker-id", &dockerClient)

	// Check the container stopped method was called successfully.
	if dockerClient.started || !dockerClient.stopped {
		t.Errorf("Expected docker client to be stopped, got started value %v and stopped value %v", dockerClient.started, dockerClient.stopped)
	}

}

func TestInitializeFilesystem(t *testing.T) {
	_, cancel := context.WithCancel(context.Background())
	defer cancel()

	err := os.RemoveAll(utils.WhistDir)
	if err != nil {
		t.Fatalf("Failed to delete directory %s for tests: error: %v\n", utils.WhistPrivateDir, err)
	}

	initializeFilesystem()

	if _, err := os.Stat(utils.WhistDir); os.IsNotExist(err) {
		t.Errorf("Whist directory was not created by initializeFilesystem")
	}

	if _, err := os.Stat(utils.WhistPrivateDir); os.IsNotExist(err) {
		t.Errorf("Whist private directory was not created by initializeFilesystem")
	}

	if _, err := os.Stat(utils.TempDir); os.IsNotExist(err) {
		t.Errorf("Whist temp directory was not created by initializeFilesystem")
	}
}

func TestUninitializeFilesystem(t *testing.T) {
	uninitializeFilesystem()

	if _, err := os.Stat(utils.WhistDir); os.IsExist(err) {
		t.Errorf("Whist directory was not removed by uninitializeFilesystem")
	}

	if _, err := os.Stat(utils.WhistPrivateDir); os.IsExist(err) {
		t.Errorf("Whist private directory was not removed by uninitializeFilesystem")
	}

	if _, err := os.Stat(utils.TempDir); os.IsExist(err) {
		t.Errorf("Whist temp directory was not removed by uninitializeFilesystem")
	}
}

// TestDrainAndShutdown will check if shutdownInstanceOnExit is set to false
func TestDrainAndShutdown(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	shutdownInstanceOnExit = false

	//  Should update the global variable to be true
	drainAndShutdown(ctx, cancel, nil)

	if !shutdownInstanceOnExit {
		t.Fatalf("error draining and shutting down. Expected shutdownInstanceOnExit = `true`, got `false`")
	}
}

// cancelMandelboxContextByID tries to look up the mandelbox by its ID
// and cancels the context associated with it.
func cancelMandelboxContextByID(id mandelboxtypes.MandelboxID) error {
	mandelbox, err := mandelbox.LookUpByMandelboxID(id)
	if err != nil {
		return utils.MakeError("could not cancel mandelbox context: %v", err)
	}
	mandelbox.Close()
	return nil
}
