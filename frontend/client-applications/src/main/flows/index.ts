import { merge, of, combineLatest, zip, from } from "rxjs"
import { map, take, filter, switchMap, share, mapTo } from "rxjs/operators"
import isEmpty from "lodash.isempty"
import pickBy from "lodash.pickby"

import authFlow, { authRefreshFlow } from "@app/main/flows/auth"
import checkPaymentFlow from "@app/main/flows/payment"
import mandelboxFlow from "@app/main/flows/mandelbox"
import autoUpdateFlow from "@app/main/flows/autoupdate"
import awsPingFlow from "@app/main/flows/ping"
import { fromTrigger, createTrigger } from "@app/main/utils/flows"
import { fromSignal, onSignal, withAppReady } from "@app/main/utils/observables"
import { persistGet } from "@app/main/utils/persist"
import { WhistTrigger } from "@app/constants/triggers"
import {
  accessToken,
  refreshToken,
  userEmail,
  configToken,
  isNewConfigToken,
  darkMode,
  timezone,
  keyRepeat,
  initialKeyRepeat,
} from "@app/main/utils/state"
import { ONBOARDED } from "@app/constants/store"
import {
  getDecryptedCookies,
  getBookmarks,
  getExtensions,
  InstalledBrowser,
} from "@app/main/utils/importer"

// Autoupdate flow
const update = autoUpdateFlow(fromTrigger(WhistTrigger.updateAvailable))

// AWS ping flow
const awsPing = awsPingFlow(of(null))

// Auth flow
const auth = authFlow(
  merge(
    fromTrigger(WhistTrigger.authInfo),
    combineLatest({
      accessToken,
      refreshToken,
      userEmail,
      configToken,
    }).pipe(
      take(1),
      filter((auth) => isEmpty(pickBy(auth, (x) => x === "")))
    )
  )
)

// Onboarding flow
const onboarded = fromSignal(
  merge(
    fromTrigger(WhistTrigger.beginImport),
    of(persistGet(ONBOARDED)).pipe(filter((onboarded) => onboarded as boolean))
  ),
  fromTrigger(WhistTrigger.authFlowSuccess)
)

// Unpack the access token to see if their payment is valid
const checkPayment = checkPaymentFlow(
  onSignal(
    fromSignal(combineLatest({ accessToken }), onboarded),
    fromTrigger(WhistTrigger.authFlowSuccess)
  )
)

// If the payment is invalid, they'll be redirect to the Stripe window. After that they'll
// get new auth credentials
const refreshAfterPaying = authRefreshFlow(
  fromSignal(
    combineLatest({ refreshToken }),
    fromTrigger(WhistTrigger.stripeAuthRefresh)
  )
)

const dontImportBrowserData = of(persistGet(ONBOARDED) as boolean).pipe(
  take(1),
  filter((onboarded: boolean) => onboarded),
  mapTo(undefined),
  share()
)

const importedData = fromTrigger(WhistTrigger.beginImport).pipe(
  switchMap((t) =>
    zip(
      from(getDecryptedCookies(t?.importBrowserDataFrom as InstalledBrowser)),
      from(getBookmarks(t?.importBrowserDataFrom as InstalledBrowser)),
      from(getExtensions(t?.importBrowserDataFrom as InstalledBrowser))
    )
  ),
  map(([cookies, bookmarks, extensions]) => ({
    cookies,
    bookmarks,
    extensions,
  })),
  share()
)

// Observable that fires when Whist is ready to be launched
const launchTrigger = onSignal(
  combineLatest({
    userEmail,
    accessToken,
    configToken,
    isNewConfigToken,
    importedData: merge(importedData, dontImportBrowserData),
    regions: merge(awsPing.cached, awsPing.refresh),
    darkMode,
    timezone,
    keyRepeat,
    initialKeyRepeat,
  }),
  merge(
    zip(
      checkPayment.success,
      of(persistGet(ONBOARDED)).pipe(
        filter((onboarded) => onboarded as boolean)
      )
    ), // On a normal launch
    zip(checkPayment.failure, refreshAfterPaying.success), // If you had an invalid subscription but now paid
    importedData // On onboarding or import
  )
).pipe(share())

// Mandelbox creation flow
const mandelbox = mandelboxFlow(withAppReady(launchTrigger))

// After the mandelbox flow is done, run the refresh flow so the tokens are being refreshed
// every time but don't impede startup time
const refreshAtEnd = authRefreshFlow(
  onSignal(combineLatest({ refreshToken }), mandelbox.success)
)

createTrigger(WhistTrigger.checkPaymentFlowFailure, checkPayment.failure)

createTrigger(WhistTrigger.mandelboxFlowStart, launchTrigger)

createTrigger(WhistTrigger.awsPingCached, awsPing.cached)
createTrigger(WhistTrigger.awsPingRefresh, awsPing.refresh)

createTrigger(WhistTrigger.authFlowSuccess, auth.success)
createTrigger(WhistTrigger.authFlowFailure, auth.failure)

createTrigger(WhistTrigger.updateDownloaded, update.downloaded)
createTrigger(WhistTrigger.downloadProgress, update.progress)

createTrigger(
  WhistTrigger.authRefreshSuccess,
  merge(refreshAtEnd.success, refreshAfterPaying.success)
)

createTrigger(WhistTrigger.mandelboxFlowSuccess, mandelbox.success)
createTrigger(WhistTrigger.mandelboxFlowFailure, mandelbox.failure)
