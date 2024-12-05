import { Socket } from "socket.io-client"
import { merge } from "rxjs"
import { withLatestFrom, filter } from "rxjs/operators"
import find from "lodash.find"

import {
  socket,
  socketConnected,
  socketDisconnected,
} from "@app/worker/events/socketio"
import { initSettingsSent } from "@app/worker/events/settings"
import { cloudTabCreated, cloudTabUpdated, tabRemoved, tabUpdated, tabZoomed } from "@app/worker/events/tabs"
import {
  webUiNavigate,
  webUiRefresh,
  webUisAreFrozen,
} from "@app/worker/events/webui"
import {
  getActiveTab,
  isCloudTab,
  markActiveCloudTab,
  unmarkActiveCloudTab,
  getTab,
} from "@app/worker/utils/tabs"
import { stateDidChange, whistState } from "@app/worker/utils/state"
import { getStorage } from "@app/worker/utils/storage"
import { Storage } from "@app/constants/storage"

import { PopupMessage, PopupMessageType } from "@app/@types/messaging"

merge(
  // Don't start creating cloud tabs until the init settings have been sent
  initSettingsSent,
  stateDidChange("waitingCloudTabs").pipe(
    withLatestFrom(socket),
    filter(
      ([change, socket]) =>
        change?.applyData?.name === "push" && socket.connected && whistState.sentInitSettings
    )
  )
)
  .pipe(withLatestFrom(socket))
  .subscribe(([, socket]: [any, Socket]) => {
    while (whistState.waitingCloudTabs.length > 0) {
      const tab = whistState.waitingCloudTabs.pop()

      if (tab !== undefined) {
        socket.emit("activate-tab", tab, true)
        markActiveCloudTab(tab)
      }

      void getActiveTab().then((activeTab) => {
        if (activeTab?.id === tab?.id) {
          void chrome.runtime.sendMessage(<PopupMessage>{
            type: PopupMessageType.SEND_POPUP_DATA,
            value: {
              isWaiting: false,
            },
          })
        }
      })
    }
  })

// Web UIs get frozen in response to tabs switching, so activate the new active tab on the server
webUisAreFrozen
  .pipe(withLatestFrom(socket))
  .subscribe(
    ([[newActiveTab, spotlightId], socket]: [
      [chrome.tabs.Tab, number],
      Socket
    ]) => {
      if (isCloudTab(newActiveTab)) {
        socket.emit("activate-tab", newActiveTab, false, spotlightId)
      }
    }
  )

// If a tab is removed, remove it on the server
tabRemoved
  .pipe(withLatestFrom(socket))
  .subscribe(([tabId, socket]: [number, Socket]) => {
    const tab = find(whistState.activeCloudTabs, (t) => t.id === tabId)
    if (tab !== undefined) {
      socket.emit("close-tab", tab)
      unmarkActiveCloudTab(tab)
    }
  })

// If a user navigates away from a cloud tab, remove it on the server
tabUpdated
  .pipe(withLatestFrom(socket))
  .subscribe(async ([tab, socket]: [chrome.tabs.Tab, Socket]) => {
    const savedCloudUrls = await getStorage<string[]>(Storage.SAVED_CLOUD_URLS)
    const url = new URL(tab?.url?.replace("cloud:", "") ?? "")
    const host = url.hostname
    const isSaved = (savedCloudUrls ?? []).includes(host)

    if (!isCloudTab(tab) && !isSaved) {
      socket.emit("close-tab", tab)
      unmarkActiveCloudTab(tab)
    }
  })

merge(cloudTabCreated, cloudTabUpdated)
  .pipe(withLatestFrom(socket))
  .subscribe(([tab, socket]: [chrome.tabs.Tab, Socket]) => {
    // Keep passing the timezone every time a tab is activated or updated
    // as a makeshift way to continuously update the timezone. This is
    // because sometimes the timezone is not set on socket connection,
    // which is where we send the first timezone-changed message.
    const timezone = Intl.DateTimeFormat().resolvedOptions().timeZone
    // If setting to the same timezone as before, the browser does not react unless we set to something different first.
    socket.emit("timezone-changed", "Etc/UTC")
    socket.emit("timezone-changed", timezone)
  })

tabZoomed
  .pipe(withLatestFrom(socket))
  .subscribe(([zoomChangeInfo, socket]: [object, Socket]) => {
    socket.emit("zoom-tab", zoomChangeInfo)
  })

webUiNavigate
  .pipe(withLatestFrom(socket))
  .subscribe(async ([message, socket]: [any, Socket]) => {
    const tab = await getTab(message.id)
    socket.emit("activate-tab", { ...tab, url: message.url }, true)
  })

webUiRefresh
  .pipe(withLatestFrom(socket))
  .subscribe(([message, socket]: [any, Socket]) => {
    socket.emit("refresh-tab", message)
  })

socketDisconnected.subscribe((socket: Socket) => {
  ;(chrome as any).whist.broadcastWhistMessage(
    JSON.stringify({
      type: "SOCKET_DISCONNECTED",
    })
  )
})

socketConnected.subscribe((socket: Socket) => {
  ;(chrome as any).whist.broadcastWhistMessage(
    JSON.stringify({
      type: "SOCKET_CONNECTED",
    })
  )
})
