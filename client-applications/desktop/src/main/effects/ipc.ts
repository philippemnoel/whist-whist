/**
 * Copyright Fractal Computers, Inc. 2021
 * @file ipc.ts
 * @brief This file contains subscriptions to Observables related to state persistence.
 */
import { eventIPC } from '@app/main/events/ipc'
import { mapValues } from 'lodash'
import { ipcBroadcast } from '@app/utils/ipc'
import { StateIPC } from '@app/@types/state'
import { combineLatest, Observable } from 'rxjs'
import { startWith, mapTo, withLatestFrom } from 'rxjs/operators'
import { WarningLoginInvalid, WarningSignupInvalid } from '@app/utils/constants'
import { getWindows } from '@app/utils/windows'
import { loginLoading, loginWarning } from '@app/main/observables/login'
import { signupLoading, signupWarning } from '@app/main/observables/signup'
import { autoUpdateDownloadProgress } from '@app/main/observables/autoupdate'

// This file is responsible for broadcasting state to all renderer windows.
// We use a single object and IPC channel for all windows, so here we set up a
// single observable subscription that calls ipcBroadcast whenever it emits.
//
// This subscription takes the latest object sent back from the renderer thread,
// and merges it with the latest data from the observables in the "subscribed"
// map below. It sends the result to all windows with ipcBroadcast.
//
// Note that combineLatest doesn't emit until each of its observable arguments
// emits an initial value. To get the state broadcasting right away, we pipe
// all the subscribed observables through startWith(undefined).
//
// We can only send serializable values over IPC, so the subscribed map is
// constrained to observables that emit serializable values.

interface SubscriptionMap {
  [key: string]: Observable<string | number | boolean | SubscriptionMap>
}

const subscribed: SubscriptionMap = {
  loginLoading: loginLoading,
  loginWarning: loginWarning.pipe(mapTo(WarningLoginInvalid)),
  updateInfo: autoUpdateDownloadProgress,
  signupLoading: signupLoading,
  signupWarning: signupWarning.pipe(mapTo(WarningSignupInvalid))
}

combineLatest(mapValues(subscribed, (o) => o.pipe(startWith(undefined))))
  .pipe(withLatestFrom(eventIPC))
  .subscribe(([subs, state]) =>
    ipcBroadcast({ ...state, ...subs } as Partial<StateIPC>, getWindows())
  )
