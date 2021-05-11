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
  containerInfoIP,
  containerInfoPorts,
  containerInfoSecretKey,
  containerInfoError,
  containerInfoSuccess,
  containerInfoPending,
} from "@app/utils/container"
import {} from "@app/utils/container"
import { loadingFrom } from "@app/utils/observables"
import { from, of, interval, merge } from "rxjs"
import { map, takeUntil, switchMap, take, mapTo, share } from "rxjs/operators"
import { gates, Flow } from "@app/utils/gates"
import { some } from "lodash"

const containerInfoGates: Flow = (name, trigger) =>
  gates(
    `${name}.containerInfoGates`,
    trigger.pipe(
      switchMap(({ containerID, accessToken }) =>
        from(containerInfo(containerID, accessToken))
      )
    ),
    {
      success: (result) => containerInfoSuccess(result),
      pending: (result) => containerInfoPending(result),
      failure: (result) =>
        containerInfoError(result) ||
        !some([containerInfoPending(result), containerInfoSuccess(result)]),
    }
  )

const containerPollingInner: Flow = (name, trigger) => {
  const tick = trigger.pipe(
    switchMap((args) => interval(1000).pipe(mapTo(args)))
  )
  const poll = containerInfoGates(name, tick)

  return {
    pending: poll.pending.pipe(takeUntil(merge(poll.success, poll.failure))),
    success: poll.success.pipe(take(1)),
    failure: poll.failure.pipe(take(1)),
  }
}

export const containerCreateFlow: Flow = (name, trigger) => {
  const next = `${name}.containerCreateFlow`

  const create = gates(
    next,
    trigger.pipe(
      switchMap(({ email, accessToken }) =>
        from(containerCreate(email, accessToken))
      )
    ),
    {
      success: (req: { json: { ID: string } }) => (req?.json?.ID ?? "") !== "",
      failure: (req: { json: { ID: string } }) => (req?.json?.ID ?? "") === "",
    }
  )

  return {
    success: create.success.pipe(
      map((response) => ({ containerID: response.json.ID }))
    ),
    failure: create.failure,
  }
}

export const containerPollingFlow: Flow = (name, trigger) => {
  const next = `${name}.containerPollingFlow`

  const poll = trigger.pipe(
    map((args) => containerPollingInner(next, of(args))),
    share()
  )

  const success = poll.pipe(switchMap((inner) => inner.success))
  const failure = poll.pipe(switchMap((inner) => inner.failure))
  const pending = poll.pipe(switchMap((inner) => inner.pending))
  const loading = loadingFrom(trigger, success, failure)

  // We probably won't subscribe to "pending" in the rest of the app,
  // so we subscribe to it deliberately here. If it has no subscribers,
  // it won't emit. We would like for it to emit for logging purposes.
  // We must remeber to takeUntil so it stops when we're finished.
  pending.pipe(takeUntil(merge(success, failure))).subscribe()

  return {
    success: success.pipe(
      map((response) => ({
        containerIP: containerInfoIP(response),
        containerSecret: containerInfoSecretKey(response),
        containerPorts: containerInfoPorts(response),
      }))
    ),
    failure,
    pending,
    loading,
  }
}
