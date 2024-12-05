package mandelbox

import (
	"bytes"
	"os"
	"path"
	"testing"

	"github.com/whisthq/whist/backend/services/host-service/mandelbox/configutils"
	"github.com/whisthq/whist/backend/services/host-service/mandelbox/portbindings"
	"github.com/whisthq/whist/backend/services/types"
	"github.com/whisthq/whist/backend/services/utils"
)

func TestWriteMandelboxParams(t *testing.T) {
	mandelbox, cancel, goroutineTracker := createTestMandelboxData()

	// Reset filesystem at the end of this test
	defer mandelbox.cleanResourceMappingDir()

	// Defer the wait first since deferred functions are executed in LIFO order.
	defer goroutineTracker.Wait()
	defer cancel()

	if err := mandelbox.AssignPortBindings([]portbindings.PortBinding{
		{MandelboxPort: 32261, HostPort: 0, BindIP: "", Protocol: "tcp"},
		{MandelboxPort: 32262, HostPort: 0, BindIP: "", Protocol: "tcp"},
		{MandelboxPort: 32263, HostPort: 0, BindIP: "", Protocol: "udp"},
		{MandelboxPort: 32273, HostPort: 0, BindIP: "", Protocol: "tcp"},
	}); err != nil {
		t.Fatalf("Error assigning port bindings: %s", err)
	}

	err := mandelbox.WriteMandelboxParams()
	if err != nil {
		t.Fatalf("Error writing mandelbox params: %v", err)
	}

	err = mandelbox.WriteProtocolTimeouts(1, 1)
	if err != nil {
		t.Fatalf("Error writing protocol timeouts: %v", err)
	}

	err = mandelbox.WriteSessionID()
	if err != nil {
		t.Fatalf("Error writing session ID: %v", err)
	}

	err = mandelbox.MarkParamsReady()
	if err != nil {
		t.Fatalf("Error writing .paramsReady: %v", err)
	}

	var paramsTests = []string{
		"hostPort_for_my_32262_tcp",
		"tty",
		"gpu_index",
		"session_id",
		"timeout",
		".paramsReady",
	}

	for _, tt := range paramsTests {
		t.Run(utils.Sprintf("%s", tt), func(t *testing.T) {

			err = verifyResourceMappingFileCreation(tt)
			if err != nil {
				t.Fatalf("Could not read file %s: %v", tt, err)
			}
		})
	}
}

// TestWriteMandelboxParamsErrors checks that the relevant Mandelbox functions
// to write parameters return the expected errors when there is some erroneous
// behavior
func TestWriteMandelboxParamsErrors(t *testing.T) {
	mandelbox, cancel, goroutineTracker := createTestMandelboxData()

	// Reset filesystem at the end of this test
	defer mandelbox.cleanResourceMappingDir()

	// Defer the wait first since deferred functions are executed in LIFO order.
	defer goroutineTracker.Wait()
	defer cancel()

	// Attempting to write the mandelbox parameters before having set the port bindings
	// for port 32262 should trigger an error
	err := mandelbox.WriteMandelboxParams()
	if err == nil {
		t.Fatalf("Writing mandelbox params before assigning the port binding for port 32262 should trigger an error (it didn't): %v", err)
	}

	if err = mandelbox.AssignPortBindings([]portbindings.PortBinding{
		{MandelboxPort: 32261, HostPort: 0, BindIP: "", Protocol: "tcp"},
		{MandelboxPort: 32262, HostPort: 0, BindIP: "", Protocol: "tcp"},
		{MandelboxPort: 32263, HostPort: 0, BindIP: "", Protocol: "udp"},
		{MandelboxPort: 32273, HostPort: 0, BindIP: "", Protocol: "tcp"},
	}); err != nil {
		t.Fatalf("Error assigning port bindings: %s", err)
	}

	// After assigning port bindings, writing mandelbox parameters should succeed
	err = mandelbox.WriteMandelboxParams()
	if err != nil {
		t.Fatalf("Error writing mandelbox params: %v", err)
	}

	mandelbox.cleanResourceMappingDir()

	var paramsTests = []string{
		"hostPort_for_my_32262_tcp",
		"tty",
		"gpu_index",
		"session_id",
		"timeout",
		".paramsReady",
	}

	resourceDir := path.Join(utils.WhistDir, utils.PlaceholderTestUUID().String(), "/mandelboxResourceMappings/")
	for _, filename := range paramsTests {
		err := os.MkdirAll(path.Join(resourceDir, filename), 0777)
		if err != nil {
			t.Fatalf("Could not create folder needed for test: %v", err)
		}
	}

	err = mandelbox.WriteProtocolTimeouts(1, 1)
	if err == nil {
		t.Fatalf("Did not get an error when writing protocol timeout to file with name identical to a folder: %v", err)
	}

	err = mandelbox.WriteSessionID()
	if err == nil {
		t.Fatalf("Did not get an error when  writing session ID to file with name identical to a folder: %v", err)
	}

	err = mandelbox.MarkParamsReady()
	if err == nil {
		t.Fatalf("Did not get an error when  writing .paramsReady to file with name identical to a folder: %v", err)
	}
}

// TestWriteMandelboxJsonData checks if the JSON data is properly created by
// calling the write function and comparing results with a manually generated cookie file
func TestWriteMandelboxJsonData(t *testing.T) {
	testMbox, _, _ := createTestMandelboxData()

	// Reset filesystem now, and at the end of this test
	testMbox.cleanResourceMappingDir()
	defer testMbox.cleanResourceMappingDir()

	sampleJsonData := `{"dark_mode":false,"desired_timezone":"America/New_York","client_dpi":192,"restore_last_session":true,"kiosk_mode":false,"initial_key_repeat":300,"key_repeat":30,"local_client":true,"user_agent":"Mozilla/5.0 (Macintosh; Intel Mac OS X 12_1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/97.0.4692.71 Safari/537.36","longitude":"103.851959","latitude":"1.290270","system_languages":"en_US","browser_languages":"en-US,en","user_locale":{"LC_COLLATE":"en_US.UTF-8","LC_CTYPE":"en_US.UTF-8","LC_MESSAGES":"en_US.UTF-8","LC_MONETARY":"en_US.UTF-8","LC_NUMERIC":"en_US.UTF-8","LC_TIME":"en_US.UTF-8"},"client_os":"darwin"}`
	deflatedJSONData, err := configutils.GzipDeflateString(string(sampleJsonData))
	if err != nil {
		t.Fatalf("could not deflate string with Gzip: %v", err)
	}

	inflatedJSONData, err := configutils.GzipInflateString(deflatedJSONData)
	if err != nil {
		t.Fatalf("Couldn't inflate string with Gzip: %s", err)
	}

	// Explicitly set the result to what we expect
	testFileContent := utils.Sprintf(`%s`, inflatedJSONData)

	resourceDir := path.Join(utils.WhistDir, utils.PlaceholderTestUUID().String(), "/mandelboxResourceMappings/")
	jsonDataFile := path.Join(resourceDir, "config.json")

	// Create destination directory if it does not exists
	if _, err := os.Stat(resourceDir); os.IsNotExist(err) {
		if err := os.MkdirAll(resourceDir, 0777); err != nil {
			t.Fatalf("Could not make dir %s. Error: %s", resourceDir, err)
		}
	}

	// Write the sample JSON data
	err = testMbox.WriteJSONData(types.JSONData(deflatedJSONData))
	if err != nil {
		t.Fatalf("Error writing config.json file for protocol: %v", err)
	}

	// Check that the file for the JSON data exists
	err = verifyResourceMappingFileCreation("config.json")
	if err != nil {
		t.Errorf("Could not create json data file config.json: %v", err)
	}

	// Check the contents of the file
	matchingFile, err := os.Open(jsonDataFile)
	if err != nil {
		t.Fatalf("error opening JSON data file config.json: %v", err)
	}

	var matchingFileBuf bytes.Buffer
	_, err = matchingFileBuf.ReadFrom(matchingFile)
	if err != nil {
		t.Fatalf("error reading config.json file: %v", err)
	}

	// Check contents match
	if string(testFileContent) != matchingFileBuf.String() {
		t.Fatalf("file contents don't match for file %s: '%s' vs '%s'", jsonDataFile, testFileContent, matchingFileBuf.Bytes())
	}
}

// TestWriteMandelboxJsonData checks that the WriteJSONData returns an error if inflated
// JSON data is passed
func TestWriteInflatedMandelboxJsonData(t *testing.T) {
	testMbox, _, _ := createTestMandelboxData()

	// Reset filesystem now, and at the end of this test
	testMbox.cleanResourceMappingDir()
	defer testMbox.cleanResourceMappingDir()

	sampleJsonData := `{"dark_mode":false,"desired_timezone":"America/New_York","client_dpi":192,"restore_last_session":true,"kiosk_mode":false,"initial_key_repeat":300,"key_repeat":30,"local_client":true,"user_agent":"Mozilla/5.0 (Macintosh; Intel Mac OS X 12_1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/97.0.4692.71 Safari/537.36","longitude":"103.851959","latitude":"1.290270","system_languages":"en_US","browser_languages":"en-US,en","user_locale":{"LC_COLLATE":"en_US.UTF-8","LC_CTYPE":"en_US.UTF-8","LC_MESSAGES":"en_US.UTF-8","LC_MONETARY":"en_US.UTF-8","LC_NUMERIC":"en_US.UTF-8","LC_TIME":"en_US.UTF-8"},"client_os":"darwin"}`

	// Write the sample JSON data
	err := testMbox.WriteJSONData(types.JSONData(sampleJsonData))
	if err == nil {
		t.Fatalf("Writing already-inflated JSON data should return an error (it didn't): %v", err)
	}
}

func verifyResourceMappingFileCreation(file string) error {
	resourceDir := path.Join(utils.WhistDir, utils.PlaceholderTestUUID().String(), "/mandelboxResourceMappings/")
	_, err := os.Stat(path.Join(resourceDir, file))
	return err
}
