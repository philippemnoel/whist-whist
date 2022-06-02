package mandelbox

import (
	"bytes"
	"encoding/json"
	"os"
	"path"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/whisthq/whist/backend/services/host-service/mandelbox/configutils"
	"github.com/whisthq/whist/backend/services/types"
	"github.com/whisthq/whist/backend/services/utils"
)

// TestUserInitialBrowserWrite checks if the browser data is properly created by
// calling the write function and comparing results with a manually generated cookie file
func TestUserInitialBrowserWrite(t *testing.T) {
	testMbox, _, _ := createTestMandelboxData()

	// Reset filesystem now, and at the end of this test
	os.RemoveAll(testMbox.GetUserConfigDir())
	defer os.RemoveAll(testMbox.GetUserConfigDir())

	// Define browser data
	testCookie1 := "{'creation_utc': 13280861983875934, 'host_key': 'test_host_key_1.com'}"
	testCookie2 := "{'creation_utc': 4228086198342934, 'host_key': 'test_host_key_2.com'}"

	// We will simulate a user with cookies but no bookmarks
	cookiesJSON := "[" + testCookie1 + "," + testCookie2 + "]"
	extensions := "not_real_extension_id,not_real_second_extension_id"

	// Create browser data
	userInitialBrowserData := BrowserData{
		CookiesJSON: types.Cookies(cookiesJSON),
		Bookmarks:   &configutils.Bookmarks{},
		Extensions:  types.Extensions(extensions),
	}

	// Explicitly set the result to what we expect
	testFileContent := utils.Sprintf(`{"cookiesJSON":"%s","extensions":"%s"}`, cookiesJSON, extensions)

	if err := testMbox.WriteUserInitialBrowserData(userInitialBrowserData); err != nil {
		t.Fatalf("error writing user initial browser data: %v", err)
	}

	// Get browser data file path
	browserDataFile := path.Join(testMbox.GetUserConfigDir(), UnpackedConfigsDirectoryName, UserInitialBrowserFile)

	matchingFile, err := os.Open(browserDataFile)
	if err != nil {
		t.Fatalf("error opening matching file %s: %v", browserDataFile, err)
	}

	var matchingFileBuf bytes.Buffer
	_, err = matchingFileBuf.ReadFrom(matchingFile)
	if err != nil {
		t.Fatalf("error reading matching file %s: %v", browserDataFile, err)
	}

	// Check contents match
	if string(testFileContent) != matchingFileBuf.String() {
		t.Fatalf("file contents don't match for file %s: '%s' vs '%s'", browserDataFile, testFileContent, matchingFileBuf.Bytes())
	}
}

// TestUserInitialBrowserWriteEmpty checks if passing empty browser data will result in an empty json file
func TestUserInitialBrowserWriteEmpty(t *testing.T) {
	testMbox, _, _ := createTestMandelboxData()

	// Reset filesystem now, and at the end of this test
	os.RemoveAll(testMbox.GetUserConfigDir())
	defer os.RemoveAll(testMbox.GetUserConfigDir())

	// Empty browser data will generate an empty json file
	if err := testMbox.WriteUserInitialBrowserData(BrowserData{}); err != nil {
		t.Fatalf("error writing user initial browser data: %v", err)
	}

	// Get browser data file path
	browserDataFile := path.Join(testMbox.GetUserConfigDir(), UnpackedConfigsDirectoryName, UserInitialBrowserFile)

	matchingFile, err := os.Open(browserDataFile)
	if err != nil {
		t.Fatalf("error opening matching file %s: %v", browserDataFile, err)
	}

	var matchingFileBuf bytes.Buffer
	_, err = matchingFileBuf.ReadFrom(matchingFile)
	if err != nil {
		t.Fatalf("error reading matching file %s: %v", browserDataFile, err)
	}

	// Check contents match
	if matchingFileBuf.String() != "{}" {
		t.Fatalf("file contents don't match for file %s: '{}' vs '%s'", browserDataFile, matchingFileBuf.Bytes())
	}
}

// TestUserInitialBrowserParse checks if the browser data is properly parsed by
// calling the relevant functions to decompress and umarshall the received data
// and comparing results with a manually generated browser data struct
func TestUserInitialBrowserParse(t *testing.T) {
	// Define browser data
	testCookie1 := "{'creation_utc': 13280861983875934, 'host_key': 'test_host_key_1.com'}"
	testCookie2 := "{'creation_utc': 4228086198342934, 'host_key': 'test_host_key_2.com'}"

	// We will simulate a user with cookies but no bookmarks
	cookiesJSON := "[" + testCookie1 + "," + testCookie2 + "]"
	extensions := "not_real_extension_id,not_real_second_extension_id"

	expectedBookmarks := configutils.Bookmarks{
		Roots: map[string]configutils.Bookmark{
			"bookmark_bar": {
				Children: []configutils.Bookmark{
					{
						DateAdded:    "1527180903",
						DateModified: "1527180903",
						Guid:         "2",
						Id:           "2",
						Name:         "😂",
						Type:         "url",
						Url:          "https://www.example.com/",
					},
					{
						Children: []configutils.Bookmark{
							{
								DateAdded:    "1527180903",
								DateModified: "1527180903",
								Guid:         "4",
								Id:           "4",
								Name:         "网站",
								Type:         "url",
								Url:          "https://www.example.com/",
							},
						},
						DateAdded:    "1527180903",
						DateModified: "1527180903",
						Guid:         "3",
						Id:           "3",
						Name:         "Nested",
						Type:         "folder",
					},
				},
				DateAdded:    "1527180903",
				DateModified: "1527180903",
				Guid:         "1",
				Id:           "1",
				Name:         "Bookmark Bar",
				Type:         "folder",
			},
			"other": {
				Children:  []configutils.Bookmark{},
				DateAdded: "12345",
				Name:      "Other Bookmarks",
				Type:      "folder",
			},
		},
		SyncMetadata: "adfsd",
		Version:      1,
	}

	// Create browser data
	userInitialBrowserData := BrowserData{
		CookiesJSON: types.Cookies(cookiesJSON),
		Bookmarks:   &expectedBookmarks,
		Extensions:  types.Extensions(extensions),
	}

	stringifiedBrowserData, err := json.Marshal(userInitialBrowserData)
	if err != nil {
		t.Fatalf("could not marshal browser data: %v", err)
	}

	deflatedBrowserData, err := configutils.GzipDeflateString(string(stringifiedBrowserData))
	if err != nil {
		t.Fatalf("could not deflate browser data: %v", err)
	}

	inflatedBrowserData, err := configutils.GzipInflateString(deflatedBrowserData)
	if err != nil || len(inflatedBrowserData) == 0 {
		t.Fatalf("Could not inflate compressed user browser data: %v", err)
	}

	// Unmarshal bookmarks into proper format
	unmarshalledBrowserData, err := UnmarshalBrowserData(types.BrowserData(inflatedBrowserData))
	if err != nil {
		t.Fatalf("Error unmarshalling user browser data: %v", err)
	}

	if !cmp.Equal(*unmarshalledBrowserData.Bookmarks, expectedBookmarks) {
		t.Fatalf("UnmarshalBrowserData returned Bookmarks %v, expected %v", unmarshalledBrowserData.Bookmarks, expectedBookmarks)
	}

	// Set Bookmarks *configutils.Bookmarks address to be the same in the two BrowserData so that the comparison below can succeed
	// (cmp.Equal does not compare structs recursively)
	unmarshalledBrowserData.Bookmarks = &expectedBookmarks

	if !cmp.Equal(unmarshalledBrowserData, userInitialBrowserData) {
		t.Fatalf("UnmarshalBrowserData returned %v, expected %v", unmarshalledBrowserData, userInitialBrowserData)
	}
}
