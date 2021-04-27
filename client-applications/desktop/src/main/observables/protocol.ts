// This file is home to observables that manage launching the protocol process
// on the user's machine. They are distinct from the observables that
// communicate with the webserver to manage container creation.
//
// Many of these observables emit the protocol ChildProcess object, which
// carries important data about the state of the protocol process.

import { protocolLaunch } from "@app/utils/protocol"
import {
  containerInfoIP,
  containerInfoPorts,
  containerInfoSecretKey,
} from "@app/utils/container"
import { debugObservables, errorObservables } from "@app/utils/logging"
import {
  containerCreateRequest,
  containerAssignSuccess,
  containerAssignFailure,
} from "@app/main/observables/container"
import { hostConfigFailure } from "@app/main/observables/host"
import { loadingFrom } from "@app/utils/observables"
import { zip, of, fromEvent, merge } from "rxjs"
import { map, filter, share, mergeMap, take } from "rxjs/operators"
import { EventEmitter } from "events"

export const protocolLaunchProcess = containerCreateRequest.pipe(
  take(1),
  map(() => protocolLaunch()),
  share()
)

export const protocolLaunchSuccess = containerAssignSuccess.pipe(
  map((res) => ({
    ip: containerInfoIP(res),
    secret_key: containerInfoSecretKey(res),
    ports: containerInfoPorts(res),
  }))
)

export const protocolLaunchFailure = merge(
  hostConfigFailure,
  containerAssignFailure
)

export const protocolLoading = loadingFrom(
  protocolLaunchProcess,
  protocolLaunchSuccess,
  protocolLaunchFailure
)

export const protocolCloseRequest = protocolLaunchProcess.pipe(
  mergeMap((protocol) =>
    zip(of(protocol), fromEvent(protocol as EventEmitter, "close"))
  )
)

export const protocolCloseFailure = protocolCloseRequest.pipe(
  filter(([protocol]) => protocol.killed)
)

export const protocolCloseSuccess = protocolCloseRequest.pipe(
  filter(([protocol]) => !protocol.killed)
)

// Logging

debugObservables(
  [protocolLaunchProcess, "protocolLaunchProcess"],
  [protocolLaunchSuccess, "protocolLaunchSuccess"],
  [protocolLoading, "protocolLaunchLoading"],
  [protocolCloseRequest, "protocolCloseRequest"]
)

errorObservables([protocolLaunchFailure, 'protocolLaunchFailure'])
