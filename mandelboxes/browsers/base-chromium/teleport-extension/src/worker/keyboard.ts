import {
  NativeHostMessage,
  NativeHostMessageType,
} from "@app/constants/ipc"

import { Socket } from "socket.io-client"

const initKeyboardRepeatRateHandler = (socket: Socket, nativeHostPort: chrome.runtime.Port) => {
  socket.on("keyboard-repeat-rate-changed", async ([repeatDelay, repeatRate]: [number, number]) => {
    // Forward the message to the native host
    nativeHostPort.postMessage(<NativeHostMessage>{
      type: NativeHostMessageType.KEYBOARD_REPEAT_RATE,
      value: { repeatDelay, repeatRate },
    })
  })
}

export { initKeyboardRepeatRateHandler }
