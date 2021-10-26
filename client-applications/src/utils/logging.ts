/**
 * Copyright 2021 Fractal Computers, Inc., dba Whist
 * @file logging.ts
 * @brief This file contains utility functions for logging.
 */

import { app } from "electron"
import fs from "fs"
import path from "path"
import util from "util"
import * as Amplitude from "@amplitude/node"
import omitDeep from "deepdash/omitDeep"

import config, {
  loggingBaseFilePath,
  loggingFiles,
} from "@app/config/environment"
import { persistGet } from "@app/utils/persist"
import { sessionID } from "@app/constants/app"
import { createLogger } from "logzio-nodejs"

app.setPath("userData", loggingBaseFilePath)

const amplitude = Amplitude.init(config.keys.AMPLITUDE_KEY)
export const electronLogPath = path.join(loggingBaseFilePath, "logs")

// Open a file handle to append to the logs file.
// Create the loggingBaseFilePath directory if it does not exist.
const openLogFile = () => {
  fs.mkdirSync(electronLogPath, { recursive: true })
  const logPath = path.join(electronLogPath, loggingFiles.client)
  return fs.createWriteStream(logPath)
}

const logzio = createLogger({
  token: config.keys.LOGZ_KEY,
})

const logFile = openLogFile()

// Log levels
export enum LogLevel {
  DEBUG = "DEBUG",
  WARNING = "WARNING",
  ERROR = "ERROR",
}

const formatLogs = (title: string, data: object, level: LogLevel) => {
  // We use a special stringify function below before converting an object
  // to JSON. This is because certain objects in our application, like HTTP
  // responses and ChildProcess objects, have circular references in their
  // structure. This is normal NodeJS behavior, but it can cause a runtime error
  // if you blindly try to turn these objects into JSON. Our special stringify
  // function strips these circular references from the object.
  const template = `${level}: ${title} -- ${sessionID.toString()} -- \n ${util.inspect(
    data
  )}`

  return `${util.format(template)} \n`
}

const localLog = (
  title: string,
  data: object,
  level: LogLevel,
  userEmail: string,
  msElapsed?: number
) => {
  const logs = formatLogs(
    `${title} -- ${userEmail} -- ${(msElapsed !== undefined
      ? msElapsed
      : 0
    ).toString()} ms since flow/trigger was started and ${(
      Date.now() - sessionID
    ).toString()} ms since app was started`,
    data,
    level
  )

  if (!app.isPackaged) console.log(logs)

  logFile.write(logs)
}

const amplitudeLog = (
  title: string,
  data: object,
  userEmail: string,
  msElapsed?: number
) => {
  if (userEmail !== "")
    amplitude
      .logEvent({
        event_type: `[${
          (config.appEnvironment as string) ?? "LOCAL"
        }] ${title}`,
        session_id: sessionID,
        user_id: userEmail,
        event_properties: {
          ...data,
          msElapsed: msElapsed !== undefined ? msElapsed : 0,
        },
      })
      .catch((err) => console.error(err))
}

export const logBase = (
  title: string,
  data: object,
  level?: LogLevel,
  msElapsed?: number
) => {
  /*
  Description:
      Sends a log to console, client.log file, and/or logz.io depending on if the app is packaged
  Arguments:
      title (string): Log title
      data (any): JSON or list
      level (LogLevel): Log level, see enum LogLevel above
  */

  // Don't log the config token to Amplitude to protect user privacy
  data = omitDeep(data, "configToken", { onMatch: { skipChildren: true } })
  data = omitDeep(data, "config_encryption_token", {
    onMatch: { skipChildren: true },
  })

  const userEmail = persistGet("userEmail") ?? ""
  localLog(title, data, level ?? LogLevel.DEBUG, userEmail as string, msElapsed)

  if (app.isPackaged) amplitudeLog(title, data, userEmail as string, msElapsed)
}

export const protocolToLogz = (line: string) => {
  // This function will push to logz.io on each log line received from the protocol
  const match = line.match(/^[\d:.]*\s*\|\s*(?<level>\w+)\s*\|/)
  const level = match?.groups?.level ?? "INFO"

  logzio.log({
    level: level,
    message: line,
    session_id: sessionID,
    component: "clientapp",
  })
}
