import { fromEvent, fromEventPattern } from "rxjs"
import { switchMap } from "rxjs/operators"
import { Socket } from "socket.io-client"

import { socket } from "@app/worker/events/socketio"

const windowFocused = fromEventPattern(
  (handler: any) => chrome.windows.onFocusChanged.addListener(handler),
  (handler: any) => chrome.windows.onFocusChanged.removeListener(handler),
  (windowId: number) => windowId
)

const serverWindowCreated = socket.pipe(
  switchMap((s: Socket) => fromEvent(s, "server-window-created"))
)

export { windowFocused, serverWindowCreated }
