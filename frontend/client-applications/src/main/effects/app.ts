/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file app.ts
 * @brief This file contains subscriptions to Electron app event emitters observables.
 */

import { app, session } from "electron"
import { autoUpdater } from "electron-updater"
import { merge } from "rxjs"
import { take } from "rxjs/operators"
import Sentry from "@sentry/electron"

import { relaunch, createOnboardingWindow } from "@app/utils/windows"
import { fromTrigger } from "@app/utils/flows"
import { persistGet, persistClear, persistSet } from "@app/utils/persist"
import { withAppReady } from "@app/utils/observables"
import {
  ONBOARDED,
  CACHED_USER_EMAIL,
  CACHED_ACCESS_TOKEN,
  CACHED_REFRESH_TOKEN,
  CACHED_CONFIG_TOKEN,
} from "@app/constants/store"
import { WhistTrigger } from "@app/constants/triggers"
import { networkAnalyze } from "@app/utils/networkAnalysis"
import { protocolStreamKill } from "@app/utils/protocol"

// If an update is available, show the update window and download the update
fromTrigger(WhistTrigger.updateAvailable).subscribe(() => {
  autoUpdater.downloadUpdate().catch((err) => Sentry.captureException(err))
})

// On signout or relaunch, clear the cache (so the user can log in again) and restart
// the app
fromTrigger(WhistTrigger.clearCacheAction).subscribe(
  (payload: { clearConfig: boolean }) => {
    persistClear([
      ...[CACHED_USER_EMAIL, CACHED_ACCESS_TOKEN, CACHED_REFRESH_TOKEN],
      ...(payload.clearConfig ? [CACHED_CONFIG_TOKEN] : []),
    ])
    // Clear the Auth0 cache. In window.ts, we tell Auth0 to store session info in
    // a partition called "auth0", so we clear the "auth0" partition here
    session
      .fromPartition("auth0")
      .clearStorageData()
      .catch((err) => Sentry.captureException(err))
    // Restart the app
    protocolStreamKill()
    relaunch()
  }
)

merge(
  fromTrigger(WhistTrigger.relaunchAction),
  fromTrigger(WhistTrigger.reactivated)
).subscribe(() => {
  protocolStreamKill()
  relaunch()
})

withAppReady(fromTrigger(WhistTrigger.authFlowSuccess))
  .pipe(take(1))
  .subscribe(() => {
    const onboarded = (persistGet(ONBOARDED) as boolean) ?? false
    if (!onboarded) {
      networkAnalyze()
      createOnboardingWindow()
    }
  })

withAppReady(fromTrigger(WhistTrigger.onboarded)).subscribe(() => {
  persistSet(ONBOARDED, true)
})

fromTrigger(WhistTrigger.onboarded).subscribe(() => {
  persistSet(ONBOARDED, true)
})

fromTrigger(WhistTrigger.appReady).subscribe(() => {
  app.requestSingleInstanceLock()

  app.on("second-instance", (e) => {
    e.preventDefault()
  })
})
