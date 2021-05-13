// This file is home to observables that manage container creation.
// Their responsibilities are to listen for state that will trigger protocol
// launches. These observables then communicate with the webserver and
// poll the state of the containers while they spin up.
//
// These observables are subscribed by protocol launching observables, which
// react to success container creation emissions from here.

import {
  containerCreate,
  containerInfo,
  containerInfoError,
  containerInfoSuccess,
  containerInfoPending,
} from "@app/utils/container"
import {
  userSub,
  userAccessToken,
  userConfigToken,
} from "@app/main/observables/user"
import { eventUpdateAvailable } from "@app/main/events/autoupdate"
import { debugObservables, errorObservables } from "@app/utils/logging"
import { ContainerAssignTimeout } from "@app/utils/constants"
import { loadingFrom, pollMap } from "@app/utils/observables"
import { from, of, zip } from "rxjs"
import {
  map,
  share,
  delay,
  filter,
  takeLast,
  takeUntil,
  exhaustMap,
  withLatestFrom,
  takeWhile,
} from "rxjs/operators"
import {
  formatContainer,
  formatTokensArray,
  formatObservable,
} from "@app/utils/formatters"

export const containerCreateRequest = zip(
  userSub,
  userAccessToken,
  userConfigToken
).pipe(
  takeUntil(eventUpdateAvailable),
  map(([email, access, _]) => [email, access])
)

export const containerCreateProcess = containerCreateRequest.pipe(
  exhaustMap(([email, token]) => from(containerCreate(email, token))),
  share()
)

export const containerCreateSuccess = containerCreateProcess.pipe(
  filter((req) => (req?.json?.ID ?? "") !== "")
)

export const containerCreateFailure = containerCreateProcess.pipe(
  filter((req) => (req?.json?.ID ?? "") === "")
)

export const containerCreateLoading = loadingFrom(
  containerCreateRequest,
  containerCreateSuccess,
  containerCreateFailure
)

export const containerAssignRequest = containerCreateSuccess.pipe(
  withLatestFrom(userAccessToken),
  map(([response, token]) => [response.json.ID, token])
)

export const containerAssignPolling = containerAssignRequest.pipe(
  pollMap(1000, async ([id, token]) => await containerInfo(id, token)),
  takeWhile((res) => containerInfoPending(res), true),
  takeWhile((res) => !containerInfoError(res), true),
  takeUntil(of(true).pipe(delay(ContainerAssignTimeout))),
  share()
)

containerAssignPolling.subscribe((res) =>
  console.log("container poll", res?.status, res?.json.state)
)

export const containerAssignSuccess = containerAssignPolling.pipe(
  takeLast(1),
  filter((res) => containerInfoSuccess(res))
)

export const containerAssignFailure = containerAssignPolling.pipe(
  takeLast(1),
  filter(
    (res) =>
      containerInfoError(res) ||
      containerInfoPending(res) ||
      !containerInfoSuccess(res)
  )
)

export const containerAssignLoading = loadingFrom(
  containerAssignRequest,
  containerAssignSuccess,
  containerAssignFailure
)

// Logging

debugObservables(
  [
    formatObservable(containerCreateRequest, formatTokensArray),
    "containerCreateRequest",
  ],
  [
    formatObservable(containerCreateSuccess, formatContainer),
    "containerCreateSuccess",
  ],
  [containerCreateLoading, "containerCreateLoading"],
  [
    formatObservable(containerAssignRequest, formatTokensArray),
    "containerAssignRequest",
  ],
  [
    formatObservable(containerAssignPolling, formatContainer),
    "containerAssignPolling",
  ],
  [
    formatObservable(containerAssignSuccess, formatContainer),
    "containerAssignSuccess",
  ],
  [containerAssignLoading, "containerAssignLoading"]
)

errorObservables(
  [
    formatObservable(containerCreateFailure, formatContainer),
    "containerCreateFailure",
  ],
  [
    formatObservable(containerAssignFailure, formatContainer),
    "containerAssignFailure",
  ]
)
