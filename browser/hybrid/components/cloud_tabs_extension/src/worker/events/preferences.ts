import { fromEvent, fromEventPattern } from "rxjs"
import { switchMap, map, share } from "rxjs/operators"
import { Socket } from "socket.io-client"

import { socket, socketConnected } from "@app/worker/events/socketio"


const darkModeInitialized = socket
    .pipe(switchMap((s: Socket) => fromEvent(s, "darkmode-initialized")))

const darkModeChanged = fromEventPattern(
  (handler: any) => (chrome as any).braveTheme.onBraveThemeTypeChanged.addListener(handler),
  (handler: any) => (chrome as any).braveTheme.onBraveThemeTypeChanged.removeListener(handler),
  (themeType: string) => themeType
).pipe(
    map((themeType: string) => themeType === "Dark"),
    share()
)

export {
    darkModeInitialized,
    darkModeChanged,
}
