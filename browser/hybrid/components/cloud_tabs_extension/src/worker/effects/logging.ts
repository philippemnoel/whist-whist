import { interval, merge } from "rxjs"
import { throttle } from "rxjs/operators"
import * as amplitude from "@amplitude/analytics-browser"
import { track, Identify, identify } from "@amplitude/analytics-browser"

import { authSuccess, authFailure } from "@app/worker/events/auth"
import {
  mandelboxNeeded,
  mandelboxError,
  mandelboxSuccess,
} from "@app/worker/events/mandelbox"
import { tabActivated, tabCreated, tabFocused } from "@app/worker/events/tabs"
import {
  socketConnected,
  socketDisconnected,
} from "@app/worker/events/socketio"
import { getStorage } from "@app/worker/utils/storage"

import { config } from "@app/constants/app"
import { AuthInfo } from "@app/@types/payload"
import { Storage } from "@app/constants/storage"

authSuccess.subscribe(async (auth: AuthInfo) => {
  console.log("Successfully logged in")

  if (config.AMPLITUDE_KEY !== undefined) {
    amplitude.init(config.AMPLITUDE_KEY, auth.userEmail)

    const event = new Identify()
    const waitlisted = await getStorage<boolean>(Storage.WAITLISTED)

    event.set("waitlisted", waitlisted)
    identify(event)
  }
})

merge(tabActivated, tabFocused, tabCreated)
  .pipe(throttle(() => interval(1000 * 60 * 60 * 12)))
  .subscribe(() => {
    console.log("Usage ping")
    track("Usage ping")
  })

authFailure.subscribe((auth: AuthInfo) => {
  console.log("Auth failure", auth)
})

mandelboxNeeded.subscribe((x: any) => {
  console.log("Mandelbox needed", x)
})

mandelboxSuccess.subscribe((x: any) => {
  console.log("Mandelbox successfully assigned", x)
})

// TODO: be more specific about where the error is coming from, because it could be from the host service
mandelboxError.subscribe((x: any) => {
  console.log("Mandelbox assign error", x)
})

socketConnected.subscribe(() => {
  console.log("Cloud tab successfully connected")
})

socketDisconnected.subscribe(() => {
  console.log("Cloud tab socket disconnected")
})
