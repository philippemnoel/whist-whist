package fractallogger // import "github.com/fractal/fractal/ecs-host-service/fractallogger"

import (
	"encoding/json"
	"log"
	"time"

	"github.com/logzio/logzio-go"
)

// Variable containing our logz.io shipping token --- filled in by linker if built on CI
var logzioShippingToken string

// We use a pointer of this type so we can check if it is nil in our logging
// functions, and therefore always call them safely.
var logzioTransport *logzioSender

// We define a custom type to be able to override the Send() function with sane
// error handling and our custom marshalling logic.
type logzioSender logzio.LogzioSender

type logzioMessageType string

const (
	logzioTypeInfo  logzioMessageType = "info"
	logzioTypeError logzioMessageType = "error"
)

func (sender *logzioSender) send(payload string, msgType logzioMessageType) {
	instanceID, _ := GetAwsInstanceID()
	amiID, _ := GetAwsAmiID()

	msg, err := json.Marshal(struct {
		AWSInstanceID string `json:"aws_instance_id"`
		AWSAmiID      string `json:"aws_ami_id"`
		Environment   string `json:"environment"`
		Message       string `json:"message"`
		Type          string `json:"type"`
	}{
		AWSInstanceID: instanceID,
		AWSAmiID:      amiID,
		Environment:   string(GetAppEnvironment()),
		Message:       payload,
		Type:          string(msgType),
	})

	if err != nil {
		log.Printf(ColorRed(Sprintf("Couldn't marshal payload for logz.io. Error: %s", err)))
		return
	}

	err = (*logzio.LogzioSender)(sender).Send(msg)
	if err != nil {
		log.Printf(ColorRed(Sprintf("Couldn't send payload to logz.io. Error: %s", err)))
		return
	}
}

func initializeLogzIO() (*logzioSender, error) {
	if usingProdLogging() {
		Info("Setting up logz.io integration.")
	} else {
		Info("Not setting up logz.io integration.")
		return nil, nil
	}

	if logzioShippingToken == "" {
		return nil, MakeError("Error initializing logz.io integration: logzioShippingToken is uninitialized")
	}

	sender, err := logzio.New(
		logzioShippingToken,
		logzio.SetUrl("https://listener.logz.io:8071"),
		logzio.SetDrainDuration(time.Second*3),
		logzio.SetCheckDiskSpace(false),
	)
	if err != nil {
		return nil, MakeError("Error initializing logz.io integration: %s", err)
	}

	return (*logzioSender)(sender), nil
}

// FlushLogzio flushes events in the Logzio queue but does not stop new ones from being recorded.
func FlushLogzio() {
	if logzioTransport != nil {
		if err := (*logzio.LogzioSender)(logzioTransport).Sync(); err != nil {
			Errorf("Unable to flush logzio: %s", err)
			return
		}

		(*logzio.LogzioSender)(logzioTransport).Drain()
	}
}

// stopAndDrainLogzio flushes events in the Logzio queue and stops new ones
// from being recorded.
func stopAndDrainLogzio() {
	if logzioTransport != nil {
		(*logzio.LogzioSender)(logzioTransport).Stop()
	}
}
