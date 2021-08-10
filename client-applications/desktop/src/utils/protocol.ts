/**
 * Copyright Fractal Computers, Inc. 2021
 * @file protocol.ts
 * @brief This file contains utility functions launching the protocol ChildProcess. This file contains
 * all the code used to interact with the protocol through the client-app. Everything -- launching
 * passing args, and closing args -- should be done here.
 */

import { app } from "electron"
import path from "path"
import fs from "fs"
import { spawn, ChildProcess } from "child_process"
import config, { loggingFiles } from "@app/config/environment"
import { electronLogPath } from "@app/utils/logging"

export let childProcess: ChildProcess | undefined
// Current time in UNIX (seconds)
let lastNackTime = Date.now() / 1000
// Track how many nacks there were
let numberOfRecentNacks = 0

const { protocolName, protocolFolder } = config

// Protocol arguments
// We send the environment so that the protocol can init sentry if necessary
const protocolParameters = {
  environment: config.sentryEnv,
}

const protocolArguments = [
  ...Object.entries(protocolParameters)
    .map(([flag, arg]) => [`--${flag}`, arg])
    .flat(),
  "--read-pipe",
]

export const protocolPath = path.join(protocolFolder, protocolName)

export const serializePorts = (ps: {
  port_32262: number
  port_32263: number
  port_32273: number
}) => `32262:${ps?.port_32262}.32263:${ps?.port_32263}.32273:${ps?.port_32273}`

export const writeStream = (
  process: ChildProcess | undefined,
  message: string
) => {
  process?.stdin?.write?.(message)
  process?.stdin?.write?.("\n")
}

// Spawn the child process with the initial arguments passed in
export const protocolLaunch = async () => {
  if (process.platform !== "win32") spawn("chmod", ["+x", protocolPath])

  // Create a pipe to the protocol logs file
  if (!fs.existsSync(electronLogPath))
    fs.mkdirSync(electronLogPath, { recursive: true })
  const protocolLogFile = fs.createWriteStream(
    path.join(electronLogPath, loggingFiles.protocol)
  )

  // In order to pipe a child process to this stream, we must wait until an underlying file
  // descriptor is created. This corresponds to the "open" event in the stream.
  await new Promise<void>((resolve) => {
    protocolLogFile.on("open", () => resolve())
  })

  const protocol = spawn(protocolPath, protocolArguments, {
    detached: false,
    // options are for [stdin, stdout, stderr]. pipe creates a pipe, ignore will ignore the
    // output. We pipe stdin since that's how we send args to the protocol. We pipe stdout
    // and later use that pipe to write logs to `protocol.log` and potentially stdout.
    stdio: ["pipe", "pipe", "ignore"],

    // On packaged macOS, the protocol is moved to the MacOS folder,
    // but expects to be in the Fractal.app root alongside the loading
    // animation PNG files.
    ...(app.isPackaged &&
      process.platform === "darwin" && {
        cwd: path.join(protocolFolder, ".."),
      }),
    ...(app.isPackaged &&
      process.platform !== "darwin" && {
        cwd: path.join(protocolFolder, "../.."),
      }),
  })

  // Pipe to protocol.log
  protocol.stdout.pipe(protocolLogFile)
  // If true, also show in terminal (for local debugging)
  if (process.env.SHOW_PROTOCOL_LOGS === "true")
    protocol.stdout.pipe(process.stdout)

  // When the protocol closes, reset the childProcess to undefined and show the app dock on MacOS
  protocol.on("close", () => {
    childProcess = undefined
  })

  childProcess = protocol
  return protocol
}

// Stream the rest of the info that the protocol needs
export const protocolStreamInfo = (info: {
  mandelboxIP: string
  mandelboxSecret: string
  mandelboxPorts: {
    port_32262: number
    port_32263: number
    port_32273: number
  }
}) => {
  writeStream(childProcess, `ports?${serializePorts(info.mandelboxPorts)}`)
  writeStream(childProcess, `private-key?${info.mandelboxSecret}`)
  writeStream(childProcess, `ip?${info.mandelboxIP}`)
  writeStream(childProcess, "finished?0")
}

export const protocolStreamKill = () => {
  // We send SIGINT just in case
  childProcess?.kill?.("SIGINT")
  writeStream(childProcess, "kill?0")
}

export const isNetworkUnstable = (message?: string) => {
  const currentTime = Date.now() / 1000

  // eslint-disable-next-line @typescript-eslint/strict-boolean-expressions
  if (message?.toString().includes("NACKING")) {
    // Check when the last nack happened
    if (currentTime - lastNackTime < 3) {
      // If the last nack happened less than three seconds ago, increase # of nacks
      numberOfRecentNacks += 1
    } else {
      // If the last nack was more than three seconds ago, reset timer
      numberOfRecentNacks = 1
      lastNackTime = currentTime
    }

    return numberOfRecentNacks > 6
  }

  return currentTime - lastNackTime < 3
}
