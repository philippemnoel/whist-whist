import {
  ContentScriptMessage,
  ContentScriptMessageType,
} from "@app/constants/ipc"
import { getOpenTabs } from "@app/utils/tabs"

import find from "lodash.find"
import { Socket } from "socket.io-client"

const initLocationHandler = (socket: Socket) => {
  // Listen for geolocation request event and send payload to client extension 
  chrome.runtime.onMessage.addListener(async (msg: ContentScriptMessage, sender: chrome.runtime.MessageSender) => {
    if (msg.type !== ContentScriptMessageType.GEOLOCATION_REQUEST) return

    console.log("received geolocation-requested from tab ", sender.tab)

    var clientTabId = undefined
    if (sender.tab) {
      const openTabs = await getOpenTabs()
      const foundTab = find(openTabs, (t) => t.tab.id === sender.tab?.id)
      clientTabId = foundTab?.clientTabId
    }

    socket.emit(
      "geolocation-requested", 
      msg.value.params, msg.value.metaTagName, clientTabId
    )
  })

  // Listen for geolocation request responses from the client
  socket.on("geolocation-request-completed", async ([success, response, metaTagName, tabId]: [boolean, any, string, number]) => {
    console.log("listening for geolocation-request-completed")
    const openTabs = await getOpenTabs()
    const foundTab = find(openTabs, (t) => t.clientTabId === tabId)

    if (foundTab) {
      chrome.tabs.sendMessage(
        tabId: foundTab.tab.id,
        message: {
          type: ContentScriptMessageType.GEOLOCATION_RESPONSE,
          value: {
            success, response, metaTagName, tabId
          } as ContentScriptMessage,
        }
      )
    }
  })
}

export { initLocationHandler }
