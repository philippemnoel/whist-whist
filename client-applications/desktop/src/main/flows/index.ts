import { merge, Observable } from "rxjs"
import { map, take } from "rxjs/operators"

import authFlow, { authRefreshFlow } from "@app/main/flows/auth"
import checkPaymentFlow from "@app/main/flows/payment"
import mandelboxFlow from "@app/main/flows/mandelbox"
import autoUpdateFlow from "@app/main/flows/autoupdate"
import configFlow from "@app/main/flows/config"
import { fromTrigger, createTrigger } from "@app/utils/flows"
import { fromSignal } from "@app/utils/observables"
import { getRegionFromArgv } from "@app/utils/region"
import { AWSRegion } from "@app/@types/aws"
import TRIGGER from "@app/utils/triggers"

// Autoupdate flow
const update = autoUpdateFlow(fromTrigger("updateAvailable"))

// If there's no update, get the auth credentials (access/refresh token)
const auth = authFlow(
  fromSignal(
    merge(
      fromSignal(fromTrigger("authInfo"), fromTrigger(TRIGGER.notPersisted)),
      fromTrigger(TRIGGER.persisted)
    ),
    merge(
      fromTrigger(TRIGGER.updateNotAvailable),
      fromTrigger(TRIGGER.updateError)
    )
  )
)

// Unpack the access token to see if their payment is valid
const checkPayment = checkPaymentFlow(fromTrigger(TRIGGER.authFlowSuccess))

// If the payment is invalid, they'll be redirect to the Stripe window. After that they'll
// get new auth credentials
const refreshAfterPaying = authRefreshFlow(
  fromTrigger(TRIGGER.stripeAuthRefresh)
)

// If the payment is valid, get or generate the config token
const config = configFlow(
  merge(
    fromTrigger(TRIGGER.checkPaymentFlowSuccess),
    refreshAfterPaying.success
  )
)

// Observable that fires when Fractal is ready to be launched
const launchTrigger = fromTrigger(TRIGGER.configFlowSuccess).pipe(
  map((x: object) => ({
    ...x, // { accessToken, configToken }
    region: getRegionFromArgv(process.argv), // AWS region, if admins want to control the region
  })),
  take(1)
) as Observable<{
  accessToken: string
  configToken: string
  region?: AWSRegion
}>

// Mandelbox creation flow
const mandelbox = mandelboxFlow(launchTrigger)

// After the mandelbox flow is done, run the refresh flow so the tokens are being refreshed
// every time but don't impede startup time
const refreshAtEnd = authRefreshFlow(
  fromSignal(launchTrigger, mandelbox.success)
)

createTrigger(TRIGGER.updateDownloaded, update.downloaded)
createTrigger(TRIGGER.downloadProgress, update.progress)

createTrigger(TRIGGER.authFlowSuccess, auth.success)
createTrigger(TRIGGER.authFlowFailure, auth.failure)
createTrigger(TRIGGER.authRefreshSuccess, refreshAtEnd.success)

createTrigger(TRIGGER.checkPaymentFlowSuccess, checkPayment.success)
createTrigger(TRIGGER.checkPaymentFlowFailure, checkPayment.failure)

createTrigger(TRIGGER.mandelboxFlowSuccess, mandelbox.success)
createTrigger(TRIGGER.mandelboxFlowFailure, mandelbox.failure)

createTrigger(TRIGGER.configFlowSuccess, config.success)
