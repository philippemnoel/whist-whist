import { fromEvent, fromEventPattern } from "rxjs"
import { switchMap, share } from "rxjs/operators"
import { NodeEventHandler } from "rxjs/internal/observable/fromEvent"
import { Socket } from "socket.io-client"

import { socket, socketConnected } from "@app/worker/events/socketio"

const languagesInitialized = socket
    .pipe(switchMap((s: Socket) => fromEvent(s, "languages-initialized")))

const languagesChanged = fromEventPattern(
    (handler: NodeEventHandler) => window.addEventListener('languagechange', handler),
    (handler: NodeEventHandler) => window.removeEventListener('languagechange', handler)
)

export {
    languagesInitialized,
    languagesChanged,
}
