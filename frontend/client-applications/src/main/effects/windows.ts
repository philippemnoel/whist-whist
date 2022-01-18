import { app, Notification } from "electron"
import { withLatestFrom, throttle, filter } from "rxjs/operators"
import { interval, of, merge } from "rxjs"
import Sentry from "@sentry/electron"
import isEmpty from "lodash.isempty"
import pickBy from "lodash.pickby"
import find from "lodash.find"

import { logBase } from "@app/utils/logging"
import { withAppReady, fromSignal } from "@app/utils/observables"
import { fromTrigger, createTrigger } from "@app/utils/flows"
import { WindowHashProtocol } from "@app/constants/windows"
import {
  createProtocolWindow,
  createAuthWindow,
  createLoadingWindow,
  createErrorWindow,
  createSignoutWindow,
  createBugTypeform,
  createSpeedtestWindow,
  createPaymentWindow,
  destroyOmnibar,
  createLicenseWindow,
} from "@app/utils/windows"
import { persistGet } from "@app/utils/persist"
import { internetWarning, rebootWarning } from "@app/utils/notification"
import { protocolStreamInfo, protocolStreamKill } from "@app/utils/protocol"
import { WhistTrigger } from "@app/constants/triggers"
import {
  CACHED_ACCESS_TOKEN,
  CACHED_CONFIG_TOKEN,
  CACHED_REFRESH_TOKEN,
  CACHED_USER_EMAIL,
  ONBOARDED,
  AWS_REGIONS_SORTED_BY_PROXIMITY,
} from "@app/constants/store"
import { networkAnalyze } from "@app/utils/networkAnalysis"
import { AWSRegion } from "@app/@types/aws"
import { LOCATION_CHANGED_ERROR } from "@app/constants/error"
import { accessToken } from "@whist/core-ts"
import { openSourceUrls } from "@app/constants/app"

// Keeps track of how many times we've tried to relaunch the protocol
const MAX_RETRIES = 3
let protocolLaunchRetries = 0
// Notifications
let internetNotification: Notification | undefined
let rebootNotification: Notification | undefined

// Immediately initialize the protocol invisibly since it can take time to warm up
createProtocolWindow().catch((err) => Sentry.captureException(err))

fromTrigger(WhistTrigger.appReady).subscribe(() => {
  internetNotification = internetWarning()
  rebootNotification = rebootWarning()
})

const quit = () => {
  logBase("Application quitting", {})
  protocolStreamKill()
  app?.quit()
}

const allWindowsClosed = fromTrigger(WhistTrigger.windowInfo).pipe(
  filter(
    (args: {
      crashed: boolean
      numberWindowsRemaining: number
      hash: string
      event: string
    }) => args.numberWindowsRemaining === 0
  )
)

allWindowsClosed.subscribe(
  (args: {
    crashed: boolean
    numberWindowsRemaining: number
    hash: string
    event: string
  }) => {
    // If they didn't crash out and didn't fill out the exit survey, show it to them
    if (
      args.hash !== WindowHashProtocol ||
      (args.hash === WindowHashProtocol && !args.crashed)
    ) {
      quit()
    }
  }
)

fromTrigger(WhistTrigger.windowInfo)
  .pipe(
    filter((args) => args.hash === WindowHashProtocol && args.event === "close")
  )
  .subscribe(() => {
    destroyOmnibar()
  })

fromTrigger(WhistTrigger.windowInfo)
  .pipe(withLatestFrom(fromTrigger(WhistTrigger.mandelboxFlowSuccess)))
  .subscribe(
    ([args, info]: [
      {
        hash: string
        crashed: boolean
        event: string
      },
      any
    ]) => {
      if (
        args.hash === WindowHashProtocol &&
        args.crashed &&
        args.event === "close"
      ) {
        if (protocolLaunchRetries < MAX_RETRIES) {
          protocolLaunchRetries = protocolLaunchRetries + 1
          createProtocolWindow()
            .then(() => {
              protocolStreamInfo(info)
              rebootNotification?.show()
              setTimeout(() => {
                rebootNotification?.close()
              }, 6000)
            })
            .catch((err) => Sentry.captureException(err))
        } else {
          // If we've already tried several times to reconnect, just show the protocol error window
          createTrigger(WhistTrigger.protocolError, of(undefined))
        }
      }
    }
  )

fromTrigger(WhistTrigger.networkUnstable)
  .pipe(throttle(() => interval(30000))) // Throttle to 30s so we don't show too often
  .subscribe((unstable: boolean) => {
    if (unstable) internetNotification?.show()
    if (!unstable) internetNotification?.close()
  })

fromTrigger(WhistTrigger.appReady).subscribe(() => {
  const authCache = {
    accessToken: (persistGet(CACHED_ACCESS_TOKEN) ?? "") as string,
    refreshToken: (persistGet(CACHED_REFRESH_TOKEN) ?? "") as string,
    userEmail: (persistGet(CACHED_USER_EMAIL) ?? "") as string,
    configToken: (persistGet(CACHED_CONFIG_TOKEN) ?? "") as string,
  }

  if (!isEmpty(pickBy(authCache, (x) => x === ""))) {
    void app?.dock?.show()
    createAuthWindow()
  }
})

withAppReady(
  merge(
    fromTrigger(WhistTrigger.checkPaymentFlowSuccess),
    fromTrigger(WhistTrigger.stripeAuthRefresh)
  )
).subscribe(() => {
  if (persistGet(ONBOARDED) as boolean) {
    networkAnalyze()
    createLoadingWindow()
  }
})

// If we detect that the user to a location where another datacenter is closer
// than the one we cached, we show them a warning to encourage them to relaunch Whist
fromSignal(
  fromTrigger(WhistTrigger.awsPingRefresh),
  fromTrigger(WhistTrigger.authRefreshSuccess)
).subscribe((regions) => {
  const previousCachedRegions = persistGet(
    AWS_REGIONS_SORTED_BY_PROXIMITY
  ) as Array<{ region: AWSRegion }>

  const previousClosestRegion = previousCachedRegions?.[0]?.region
  const currentClosestRegion = regions?.[0]?.region

  if (previousClosestRegion === undefined || currentClosestRegion === undefined)
    return

  // If the cached closest AWS region and new closest AWS region are the same, don't do anything
  if (previousClosestRegion === currentClosestRegion) return

  // If the difference in ping time to the cached closest AWS region vs. ping time
  // to the new closest AWS region is less than 25ms, don't do anything
  const previousClosestRegionPingTime = find(
    regions,
    (r) => r.region === previousClosestRegion
  )?.pingTime

  const currentClosestRegionPingTime = regions?.[0]?.pingTime

  if (previousClosestRegionPingTime - currentClosestRegionPingTime < 25) return

  // The closest AWS regions are different and ping times are more than 25ms apart,
  // show the user a warning window to relaunch Whist in the new AWS region
  setTimeout(() => {
    createErrorWindow(LOCATION_CHANGED_ERROR, false)
  }, 5000)
})

withAppReady(fromTrigger(WhistTrigger.showSignoutWindow)).subscribe(() => {
  destroyOmnibar()
  createSignoutWindow()
})

withAppReady(fromTrigger(WhistTrigger.showSupportWindow)).subscribe(() => {
  destroyOmnibar()
  createBugTypeform()
})

withAppReady(fromTrigger(WhistTrigger.showSpeedtestWindow)).subscribe(() => {
  destroyOmnibar()
  createSpeedtestWindow()
})

withAppReady(fromTrigger(WhistTrigger.showLicenseWindow)).subscribe(() => {
  destroyOmnibar()
  openSourceUrls.forEach((url) => createLicenseWindow(url))
})

// eslint-disable-next-line @typescript-eslint/no-misused-promises
withAppReady(fromTrigger(WhistTrigger.showPaymentWindow)).subscribe(() => {
  const accessToken = persistGet(CACHED_ACCESS_TOKEN) as string

  destroyOmnibar()

  createPaymentWindow({
    accessToken,
  }).catch((err) => Sentry.captureException(err))
})

// eslint-disable-next-line @typescript-eslint/no-misused-promises
withAppReady(fromTrigger(WhistTrigger.checkPaymentFlowFailure)).subscribe(
  ({ accessToken }: accessToken) => {
    createPaymentWindow({
      accessToken,
    }).catch((err) => Sentry.captureException(err))
  }
)
