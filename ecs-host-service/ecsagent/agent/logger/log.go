// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License"). You may
// not use this file except in compliance with the License. A copy of the
// License is located at
//
//	http://aws.amazon.com/apache2.0/
//
// or in the "license" file accompanying this file. This file is distributed
// on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
// express or implied. See the License for the specific language governing
// permissions and limitations under the License.

package logger

import (
	"fmt"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/cihub/seelog"
)

const (
	LOGLEVEL_ENV_VAR             = "ECS_LOGLEVEL"
	LOGLEVEL_ON_INSTANCE_ENV_VAR = "ECS_LOGLEVEL_ON_INSTANCE"
	LOGFILE_ENV_VAR              = "ECS_LOGFILE"
	LOG_DRIVER_ENV_VAR           = "ECS_LOG_DRIVER"
	LOG_ROLLOVER_TYPE_ENV_VAR    = "ECS_LOG_ROLLOVER_TYPE"
	LOG_OUTPUT_FORMAT_ENV_VAR    = "ECS_LOG_OUTPUT_FORMAT"
	LOG_MAX_FILE_SIZE_ENV_VAR    = "ECS_LOG_MAX_FILE_SIZE_MB"
	LOG_MAX_ROLL_COUNT_ENV_VAR   = "ECS_LOG_MAX_ROLL_COUNT"

	DEFAULT_LOGLEVEL                         = "info"
	DEFAULT_LOGLEVEL_WHEN_DRIVER_SET         = "off"
	DEFAULT_ROLLOVER_TYPE                    = "date"
	DEFAULT_OUTPUT_FORMAT                    = "logfmt"
	DEFAULT_MAX_FILE_SIZE            float64 = 10
	DEFAULT_MAX_ROLL_COUNT           int     = 24
)

type logConfig struct {
	RolloverType  string
	MaxRollCount  int
	MaxFileSizeMB float64
	logfile       string
	driverLevel   string
	instanceLevel string
	outputFormat  string
	lock          sync.Mutex
}

var Config *logConfig

func logfmtFormatter(params string) seelog.FormatterFunc {
	return func(message string, level seelog.LogLevel, context seelog.LogContextInterface) interface{} {
		return fmt.Sprintf(`level=%s time=%s msg=%q module=%s
`, level.String(), context.CallTime().UTC().Format(time.RFC3339), message, context.FileName())
	}
}

func jsonFormatter(params string) seelog.FormatterFunc {
	return func(message string, level seelog.LogLevel, context seelog.LogContextInterface) interface{} {
		return fmt.Sprintf(`{"level": %q, "time": %q, "msg": %q, "module": %q}
`, level.String(), context.CallTime().UTC().Format(time.RFC3339), message, context.FileName())
	}
}

func reloadConfig() {
	logger, err := seelog.LoggerFromConfigAsString(seelogConfig())
	if err == nil {
		seelog.ReplaceLogger(logger)
	} else {
		seelog.Error(err)
	}
}

func seelogConfig() string {
	c := `
<seelog type="asyncloop">
	<outputs formatid="` + Config.outputFormat + `">
		<filter levels="` + getLevelList(Config.driverLevel) + `">
			<console />`
	c += platformLogConfig()
	c += `
		</filter>`
	if Config.logfile != "" {
		c += `
		<filter levels="` + getLevelList(Config.instanceLevel) + `">`
		if Config.RolloverType == "size" {
			c += `
			<rollingfile filename="` + Config.logfile + `" type="size"
			 maxsize="` + strconv.Itoa(int(Config.MaxFileSizeMB*1000000)) + `" archivetype="none" maxrolls="` + strconv.Itoa(Config.MaxRollCount) + `" />`
		} else {
			c += `
			<rollingfile filename="` + Config.logfile + `" type="date"
			 datepattern="2006-01-02-15" archivetype="none" maxrolls="` + strconv.Itoa(Config.MaxRollCount) + `" />`
		}
		c += `
		</filter>`
	}
	c += `
	</outputs>
	<formats>
		<format id="logfmt" format="%EcsAgentLogfmt" />
		<format id="json" format="%EcsAgentJson" />
		<format id="windows" format="%Msg" />
	</formats>
</seelog>`

	return c
}

func getLevelList(fileLevel string) string {
	levelLists := map[string]string{
		"debug":    "debug,info,warn,error,critical",
		"info":     "info,warn,error,critical",
		"warn":     "warn,error,critical",
		"error":    "error,critical",
		"critical": "critical",
		"off":      "off",
	}
	return levelLists[fileLevel]
}

// SetLevel sets the log levels for logging
func SetLevel(driverLogLevel, instanceLogLevel string) {
	levels := map[string]string{
		"debug": "debug",
		"info":  "info",
		"warn":  "warn",
		"error": "error",
		"crit":  "critical",
		"none":  "off",
	}

	parsedDriverLevel, driverOk := levels[strings.ToLower(driverLogLevel)]
	parsedInstanceLevel, instanceOk := levels[strings.ToLower(instanceLogLevel)]

	if instanceOk || driverOk {
		Config.lock.Lock()
		defer Config.lock.Unlock()
		if instanceOk {
			Config.instanceLevel = parsedInstanceLevel
		}
		if driverOk {
			Config.driverLevel = parsedDriverLevel
		}
		reloadConfig()
	}
}

// GetLevel gets the log level
func GetLevel() string {
	Config.lock.Lock()
	defer Config.lock.Unlock()

	return Config.driverLevel
}

func setInstanceLevelDefault() string {
	if logDriver := os.Getenv(LOG_DRIVER_ENV_VAR); logDriver != "" {
		return DEFAULT_LOGLEVEL_WHEN_DRIVER_SET
	}
	if loglevel := os.Getenv(LOGLEVEL_ENV_VAR); loglevel != "" {
		return loglevel
	}
	return DEFAULT_LOGLEVEL
}

func init() {
	Config = &logConfig{
		logfile:       os.Getenv(LOGFILE_ENV_VAR),
		driverLevel:   DEFAULT_LOGLEVEL,
		instanceLevel: setInstanceLevelDefault(),
		RolloverType:  DEFAULT_ROLLOVER_TYPE,
		outputFormat:  DEFAULT_OUTPUT_FORMAT,
		MaxFileSizeMB: DEFAULT_MAX_FILE_SIZE,
		MaxRollCount:  DEFAULT_MAX_ROLL_COUNT,
	}
}

// Fractal-specific change: added the argument here to be able to replace the
// default ecs-agent logger with one defined by the host service (so the
// logging is unified).
func InitSeelog(loggerToUse seelog.LoggerInterface) {
	if err := seelog.RegisterCustomFormatter("EcsAgentLogfmt", logfmtFormatter); err != nil {
		seelog.Error(err)
	}
	if err := seelog.RegisterCustomFormatter("EcsAgentJson", jsonFormatter); err != nil {
		seelog.Error(err)
	}

	SetLevel(os.Getenv(LOGLEVEL_ENV_VAR), os.Getenv(LOGLEVEL_ON_INSTANCE_ENV_VAR))

	if RolloverType := os.Getenv(LOG_ROLLOVER_TYPE_ENV_VAR); RolloverType != "" {
		Config.RolloverType = RolloverType
	}
	if outputFormat := os.Getenv(LOG_OUTPUT_FORMAT_ENV_VAR); outputFormat != "" {
		Config.outputFormat = outputFormat
	}
	if MaxRollCount := os.Getenv(LOG_MAX_ROLL_COUNT_ENV_VAR); MaxRollCount != "" {
		i, err := strconv.Atoi(MaxRollCount)
		if err == nil {
			Config.MaxRollCount = i
		} else {
			seelog.Error("Invalid value for "+LOG_MAX_ROLL_COUNT_ENV_VAR, err)
		}
	}
	if MaxFileSizeMB := os.Getenv(LOG_MAX_FILE_SIZE_ENV_VAR); MaxFileSizeMB != "" {
		f, err := strconv.ParseFloat(MaxFileSizeMB, 64)
		if err == nil {
			Config.MaxFileSizeMB = f
		} else {
			seelog.Error("Invalid value for "+LOG_MAX_FILE_SIZE_ENV_VAR, err)
		}
	}

	// Fractal-specific change: replace these two lines:
	// 		registerPlatformLogger()
	// 		reloadConfig()
	// with this:
	seelog.ReplaceLogger(loggerToUse)
}
