/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file ipc.ts
 * @brief This file contains effects that use IPC to communicate with the renderer
 */

import { BrowserWindow } from "electron"

import { combineLatest, concat, of, merge, from, pluck } from "rxjs"
import {
  map,
  mapTo,
  startWith,
  filter,
  withLatestFrom,
  switchMap,
} from "rxjs/operators"
import mapValues from "lodash.mapvalues"

import { ipcBroadcastState } from "@app/main/utils/ipc"
import { StateIPC } from "@app/@types/state"
import { fromTrigger } from "@app/main/utils/flows"
import { appEnvironment } from "../../../config/configs"
import { WhistTrigger } from "@app/constants/triggers"
import {
  ALLOW_NON_US_SERVERS,
  RESTORE_LAST_SESSION,
  WHIST_IS_DEFAULT_BROWSER,
} from "@app/constants/store"
import { persistGet } from "@app/main/utils/persist"
import { getOtherBrowserWindows } from "@app/main/utils/applescript"
import { browsers } from "@app/main/utils/state"

// This file is responsible for broadcasting state to all renderer windows.
// We use a single object and IPC channel for all windows, so here we set up a
// single observable subscription that calls ipcBroadcastState whenever it emits.
//
// This subscription takes the latest object sent back from the renderer thread,
// and merges it with the latest data from the observables in the "subscribed"
// map below. It sends the result to all windows with ipcBroadcastState.
//
// Note that combineLatest doesn't emit until each of its observable arguments
// emits an initial value. To get the state broadcasting right away, we pipe
// all the subscribed observables through startWith(undefined).
//
// We can only send serializable values over IPC, so the subscribed map is
// constrained to observables that emit serializable values.

const subscribed = combineLatest(
  mapValues(
    {
      userEmail: merge(
        fromTrigger(WhistTrigger.authFlowSuccess),
        fromTrigger(WhistTrigger.authRefreshSuccess)
      ).pipe(
        filter((args: { userEmail?: string }) => args.userEmail !== undefined),
        map((args: { userEmail?: string }) => args.userEmail as string)
      ),
      subscriptionStatus: fromTrigger(
        WhistTrigger.checkPaymentFlowSuccess
      ).pipe(pluck("subscriptionStatus"), startWith(undefined)),
      appEnvironment: of(appEnvironment),
      updateInfo: merge(
        fromTrigger(WhistTrigger.downloadProgress).pipe(
          map((obj) => JSON.stringify(obj))
        ),
        fromTrigger(WhistTrigger.updateDownloaded).pipe(
          mapTo(
            JSON.stringify({
              percent: 100,
            })
          )
        )
      ),
      browsers: browsers,
      networkInfo: combineLatest([
        fromTrigger(WhistTrigger.networkAnalysisEvent),
        fromTrigger(WhistTrigger.awsPingRefresh),
      ]).pipe(
        map(([stats, regions]) => ({
          ...stats,
          ping: regions?.[0].pingTime,
        }))
      ),
      isDefaultBrowser: fromTrigger(WhistTrigger.storeDidChange).pipe(
        map(() => persistGet(WHIST_IS_DEFAULT_BROWSER) ?? false),
        startWith(persistGet(WHIST_IS_DEFAULT_BROWSER) ?? false)
      ),
      restoreLastSession: fromTrigger(WhistTrigger.storeDidChange).pipe(
        map(() => persistGet(RESTORE_LAST_SESSION) ?? false),
        startWith(persistGet(RESTORE_LAST_SESSION) ?? false)
      ),
      allowNonUSServers: fromTrigger(WhistTrigger.storeDidChange).pipe(
        map(() => persistGet(ALLOW_NON_US_SERVERS) ?? false),
        startWith(persistGet(ALLOW_NON_US_SERVERS) ?? false)
      ),
      otherBrowserWindows: merge(
        fromTrigger(WhistTrigger.getOtherBrowserWindows).pipe(
          switchMap((payload: { browser: string }) =>
            from(getOtherBrowserWindows(payload.browser))
          )
        ),
        fromTrigger(WhistTrigger.importTabs).pipe(mapTo(undefined))
      ),
      regions: fromTrigger(WhistTrigger.awsPingRefresh).pipe(
        map((regions) => regions?.map((r: any) => r.region))
      ),
      platform: of(process.platform),
    },
    (obs) => concat(of(undefined), obs)
  )
)

const finalState = combineLatest([
  subscribed,
  fromTrigger(WhistTrigger.eventIPC).pipe(startWith({})),
])

finalState.subscribe(
  ([subs, state]: [Partial<StateIPC>, Partial<StateIPC>]) => {
    ipcBroadcastState(
      { ...state, ...subs } as Partial<StateIPC>,
      BrowserWindow.getAllWindows()
    )
  }
)

fromTrigger(WhistTrigger.emitIPC)
  .pipe(withLatestFrom(finalState))
  .subscribe(
    ([, [subs, state]]: [any, [Partial<StateIPC>, Partial<StateIPC>]]) => {
      ipcBroadcastState(
        { ...state, ...subs } as Partial<StateIPC>,
        BrowserWindow.getAllWindows()
      )
    }
  )
