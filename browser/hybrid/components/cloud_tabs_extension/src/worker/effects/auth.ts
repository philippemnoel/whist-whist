import { merge } from "rxjs"
import { filter, take } from "rxjs/operators"

import { authSuccess, authFailure } from "@app/worker/events/auth"
import { mandelboxError } from "@app/worker/events/mandelbox"

import { getStorage, setStorage } from "@app/worker/utils/storage"
import { whistState } from "@app/worker/utils/state"
import { mandelboxCreateErrorUnauthorized } from "@app/worker/utils/mandelbox"
import { getActiveTab, updateTabUrl } from "@app/worker/utils/tabs"

import { PopupMessage, PopupMessageType } from "@app/@types/messaging"
import { AuthInfo } from "@app/@types/payload"
import { Storage } from "@app/constants/storage"
import { welcomePageOpened } from "@app/worker/events/webui"

import { config } from "@app/constants/app"

// Tell the browser that auth succeeded and redirect to chrome://welcome
authSuccess.subscribe(async (auth: AuthInfo) => {
  whistState.isLoggedIn = true

  if (auth.isFirstAuth) {
    const { id, url } = await getActiveTab()
    if ((url ?? "").includes(config.AUTH0_REDIRECT_URL)) {
      void updateTabUrl(id ?? -1, "chrome://welcome")
    } else {
      void chrome.tabs.create({ url: "chrome://welcome" })
    }
  }

  void chrome.runtime.sendMessage(<PopupMessage>{
    type: PopupMessageType.SEND_POPUP_DATA,
    value: {
      isLoggedIn: true,
    },
  })

  void setStorage(Storage.AUTH_INFO, auth)
  ;(chrome as any).whist.broadcastWhistMessage(
    JSON.stringify({
      type: "IS_LOGGED_IN",
      value: {
        loggedIn: true,
      },
    })
  )
})

// Tell the browser that auth failed
merge(
  authFailure,
  mandelboxError.pipe(
    filter((response) => mandelboxCreateErrorUnauthorized(response))
  ),
).subscribe(async () => {
  whistState.isLoggedIn = false
  ;(chrome as any).whist.broadcastWhistMessage(
    JSON.stringify({
      type: "IS_LOGGED_IN",
      value: {
        loggedIn: false,
      },
    })
  )

  void chrome.runtime.sendMessage(<PopupMessage>{
    type: PopupMessageType.SEND_POPUP_DATA,
    value: {
      isLoggedIn: false,
    },
  })
})

authFailure.pipe(take(1)).subscribe(async () => {
  const signinShown =
    (await getStorage<boolean | undefined>(Storage.SIGNIN_SHOWN)) ?? false
  if (!signinShown) {
    void chrome.tabs.create({ url: "chrome://welcome" })
    void setStorage(Storage.SIGNIN_SHOWN, true)
  }
})

welcomePageOpened.subscribe(() => {
  ;(chrome as any).whist.broadcastWhistMessage(
    JSON.stringify({
      type: "IS_LOGGED_IN",
      value: {
        loggedIn: whistState.isLoggedIn,
      },
    })
  )
})
