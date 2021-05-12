// This file is home to observables that manage container creation.
// Their responsibilities are to listen for state that will trigger protocol
// launches. These observables then communicate with the webserver and
// poll the state of the containers while they spin up.
//
// These observables are subscribed by protocol launching observables, which
// react to success container creation emissions from here.

import { from, of, interval, merge } from "rxjs"
import { map, takeUntil, switchMap, take, mapTo, share } from "rxjs/operators"
import { some } from "lodash"

import {
  containerPolling,
  containerPollingIP,
  containerPollingPorts,
  containerPollingSecretKey,
  containerPollingError,
  containerPollingSuccess,
  containerPollingPending,
} from "@app/main/utils/containerPolling"
import { loadingFrom } from "@app/utils/observables"
import { fork, flow } from "@app/main/utils/flows"

const containerPollingRequest = flow("containerPollingRequest", (trigger) =>
  fork(
    trigger.pipe(
      switchMap(({ containerID, accessToken }) =>
        from(containerPolling(containerID, accessToken))
      )
    ),
    {
      success: (result) => containerPollingSuccess(result),
      pending: (result) => containerPollingPending(result),
      failure: (result) =>
        containerPollingError(result) ||
        !some([containerPollingPending(result), containerPollingSuccess(result)]),
    }
  )
)

const containerPollingInner = flow("containerPollingInner", (trigger) => {
  const tick = trigger.pipe(
    switchMap((args) => interval(1000).pipe(mapTo(args)))
  )
  const poll = containerPollingRequest(tick)

  return {
    pending: poll.pending.pipe(takeUntil(merge(poll.success, poll.failure))),
    success: poll.success.pipe(take(1)),
    failure: poll.failure.pipe(take(1)),
  }
})

export default flow(
  "containerPollingFlow",
  (trigger) => {
    const poll = trigger.pipe(
      map((args) => containerPollingInner(of(args))),
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
          containerIP: containerPollingIP(response),
          containerSecret: containerPollingSecretKey(response),
          containerPorts: containerPollingPorts(response),
        }))
      ),
      failure,
      pending,
      loading,
    }
  }
)
