/*
Package metadata exposes some metadata about the build and run environments of the host service.
*/
package metadata // import "github.com/whisthq/whist/backend/services/metadata"

// Variable for hash of last Git commit --- filled in by linker
var gitCommit string

// GetGitCommit returns the git commit hash of this build.
func GetGitCommit() string {
	return gitCommit
}
