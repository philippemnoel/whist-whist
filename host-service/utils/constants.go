package utils // import "github.com/fractal/fractal/host-service/utils"

import "github.com/google/uuid"

// This block contains some constants for directories we use in the host
// service. They're used in a lot of packages, so we put them in the least
// common denominator --- this package.
const (
	WhistDir                 string = "/fractal/"
	TempDir                  string = WhistDir + "temp/"
	WhistPrivateDir          string = "/whist/"
	UserInitialBrowserDir    string = WhistDir + "userConfigs/"
	UserInitialCookiesFile   string = "user-initial-cookies"
	UserInitialBookmarksFile string = "user-initial-bookmarks"
)

// Note: We use these values as placeholder UUIDs because they are obvious and immediate
// when parsing/searching through logs, and by using a non nil placeholder UUID we are
// able to detect the error when a UUID is nil.

// PlaceholderWarmupUUID returns the special uuid to use as a placholder during warmup.
func PlaceholderWarmupUUID() uuid.UUID {
	uuid, _ := uuid.Parse("11111111-1111-1111-1111-111111111111")
	return uuid
}

// PlaceholderTestUUID returns the special uuid to use as a placholder during tests.
func PlaceholderTestUUID() uuid.UUID {
	uuid, _ := uuid.Parse("22222222-2222-2222-2222-222222222222")
	return uuid
}
