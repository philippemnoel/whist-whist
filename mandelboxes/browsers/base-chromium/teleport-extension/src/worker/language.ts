import {
  ContentScriptMessage,
  ContentScriptMessageType,
} from "@app/constants/ipc"

import { Socket } from "socket.io-client"

const initLanguageInitHandler = (socket: Socket) => {
  socket.on("init-languages", ([language, languages]: [string, [string]]) => {
    chrome.storage.local.set({
      language: language,
      languages: languages,
    }, () => socket.emit("languages-initialized"))
  })
}

const initLanguageChangeHandler = (socket: Socket) => {
  socket.on("language-changed", ([language, languages]: [string, [string]]) => {
    chrome.storage.local.set({
      language: language,
      languages: languages,
    })
    // When language has changed, tell all tabs about the updated languages.
    chrome.tabs.query({}, (tabs) => {
      tabs.forEach((tab) => {
        if (tab.id) {
          chrome.tabs.sendMessage(
            tab.id,
            {
              type: ContentScriptMessageType.LANGUAGE_UPDATE,
              value: {
                language, languages
              }
            } as ContentScriptMessage,
          )
        }
      })
    })
  })
}

export {
  initLanguageInitHandler,
  initLanguageChangeHandler,
}
