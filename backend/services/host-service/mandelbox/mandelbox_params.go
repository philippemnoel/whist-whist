package mandelbox // import "github.com/whisthq/whist/backend/services/host-service/mandelbox"

import (
	"os"
	"path"

	"github.com/whisthq/whist/backend/services/host-service/mandelbox/configutils"
	types "github.com/whisthq/whist/backend/services/types"
	"github.com/whisthq/whist/backend/services/utils"
	logger "github.com/whisthq/whist/backend/services/whistlogger"
)

func (mandelbox *mandelboxData) WriteMandelboxParams() error {
	var err error

	// Write identifying host port
	p, err := mandelbox.GetIdentifyingHostPort()
	if err != nil {
		return utils.MakeError("couldn't write mandelbox params: %s", err)
	}
	if err = mandelbox.writeResourceMappingToFile("hostPort_for_my_32262_tcp", utils.Sprintf("%d", p)); err != nil {
		// Don't need to wrap err here because it already contains the relevant info
		return err
	}

	// Write TTY. Note that we use `mandelbox.GetTTY()` instead of `mandelbox.tty` for the
	// locking.
	if err := mandelbox.writeResourceMappingToFile("tty", utils.Sprintf("%d", mandelbox.GetTTY())); err != nil {
		// Don't need to wrap err here because it already contains the relevant info
		return err
	}

	// Write GPU Index. Note that we use `mandelbox.GetGPU()` instead of `mandelbox.gpuIndex` for
	// the locking.
	if err := mandelbox.writeResourceMappingToFile("gpu_index", utils.Sprintf("%d", mandelbox.GetGPU())); err != nil {
		// Don't need to wrap err here because it already contains the relevant info
		return err
	}

	return nil
}

func (mandelbox *mandelboxData) WriteProtocolTimeouts(connectSeconds int, disconnectSeconds int) error {
	err := mandelbox.writeResourceMappingToFile("connect_timeout", utils.Sprintf("%v", connectSeconds))
	if err == nil {
		err = mandelbox.writeResourceMappingToFile("disconnect_timeout", utils.Sprintf("%v", disconnectSeconds))
	}
	return err
}

// WriteSessionID writes the session id received from the client to a file,
// so it can be used for logging inside the mandelbox.
func (mandelbox *mandelboxData) WriteSessionID() error {
	return mandelbox.writeResourceMappingToFile("session_id", utils.Sprintf("%v", mandelbox.GetSessionID()))
}

// WriteJSONData writes the data received through JSON transport
// to the config.json file located on the resourceMappingDir.
func (mandelbox *mandelboxData) WriteJSONData(data types.JSONData) error {
	jsonDataPlainText, err := configutils.GzipInflateString(string(data))
	if err != nil {
		logger.Warningf("Couldn't inflate JSON Data, will use string as is.")
		jsonDataPlainText = string(data)
	}

	return mandelbox.writeResourceMappingToFile("config.json", jsonDataPlainText)
}

// MarkParamsReady indicates that the mandelbox's services can be started
func (mandelbox *mandelboxData) MarkParamsReady() error {
	return mandelbox.writeResourceMappingToFile(".paramsReady", ".paramsReady")
}

func (mandelbox *mandelboxData) getResourceMappingDir() string {
	return path.Join(utils.WhistDir, mandelbox.GetID().String(), "mandelboxResourceMappings")
}

func (mandelbox *mandelboxData) createResourceMappingDir() error {
	err := os.MkdirAll(mandelbox.getResourceMappingDir(), 0777)
	if err != nil {
		return utils.MakeError("failed to create dir %s: %s", mandelbox.getResourceMappingDir(), err)
	}
	return nil
}

func (mandelbox *mandelboxData) cleanResourceMappingDir() {
	err := os.RemoveAll(mandelbox.getResourceMappingDir())
	if err != nil {
		logger.Errorf("failed to remove dir %s: %s", mandelbox.getResourceMappingDir(), err)
	}
}

func (mandelbox *mandelboxData) writeResourceMappingToFile(filename, data string) (err error) {
	file, err := os.OpenFile(path.Join(mandelbox.getResourceMappingDir(), filename), os.O_CREATE|os.O_RDWR|os.O_TRUNC, 0777)
	if err != nil {
		return utils.MakeError("unable to create file %s to store resource assignment: %s", filename, err)
	}
	// Instead of deferring the close() and sync() of the file, as is
	// conventional, we do it at the end of the function to avoid some annoying
	// linter errors
	_, err = file.WriteString(data)
	if err != nil {
		return utils.MakeError("couldn't write assignment with data %s to file %s: %s", data, filename, err)
	}

	err = file.Sync()
	if err != nil {
		return utils.MakeError("couldn't sync file %s: %v", filename, err)
	}

	err = file.Close()
	if err != nil {
		return utils.MakeError("couldn't close file %s: %v", filename, err)
	}

	logger.Infof("Wrote data \"%s\" to file %s\n", data, filename)
	return nil
}
