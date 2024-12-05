import { fromEvent, of, zip, map, filter, fromEventPattern } from "rxjs"
import { switchMap, share } from "rxjs/operators"
import { Socket } from "socket.io-client"

import { socket } from "@app/worker/events/socketio"

const geolocationRequested = socket
  .pipe(switchMap((s: Socket) => fromEvent(s, "geolocation-requested")))
  .pipe(share())

const geolocationRequestCompleted = fromEventPattern(
  (handler: any) => (chrome as any).whist.onMessage.addListener(handler),
  (handler: any) => (chrome as any).whist.onMessage.removeListener(handler),
  (message: any) => message
).pipe(
  map((message: string) => JSON.parse(message)),
  filter((message: any) => message.type === "GEOLOCATION_REQUEST_COMPLETED"),
  switchMap((message: any) =>
    zip([
      of(message.value.success),
      of(message.value.response),
      of(message.value.metaTagName),
      of(message.value.tabId),
    ])
  ),
  share()
)

export {
  geolocationRequested,
  geolocationRequestCompleted,
}
