import {
  NativeHostMessage,
  NativeHostMessageType,
} from "@app/constants/ipc"

import { Socket } from "socket.io-client"

const initPreferencesInitHandler = (socket: Socket) => {
    socket.on("init-preferences", ([isDarkMode]: [boolean]) => {
        ;(chrome as any).runtime.setDarkMode(isDarkMode, () => socket.emit("darkmode-initialized"))
    })
}

const initDarkModeChangeHandler = (socket: Socket) => {
    socket.on("darkmode-changed", ([isDarkMode]: [boolean]) => {
        ;(chrome as any).runtime.setDarkMode(isDarkMode)
    })
}

const initTimezoneChangeHandler = (socket: Socket, nativeHostPort: chrome.runtime.Port) => {
  socket.on("timezone-changed", async ([timezone]: [string]) => {
    // Forward the message to the native host
    nativeHostPort.postMessage(<NativeHostMessage>{
      type: NativeHostMessageType.TIMEZONE,
      value: timezone,
    })
  })
}

export {
    initPreferencesInitHandler,
    initDarkModeChangeHandler,
    initTimezoneChangeHandler,
}
