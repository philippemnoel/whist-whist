import { fromEvent, of, merge } from "rxjs"
import {
  map,
  switchMap,
  filter,
  share,
  count,
  take,
  withLatestFrom,
} from "rxjs/operators"

import { mandelboxSuccess } from "@app/worker/events/mandelbox"
import { io, Socket } from "socket.io-client"
import { MandelboxInfo, HostInfo } from "@app/@types/payload"
import { JQueryStyleEventEmitter } from "rxjs/internal/observable/fromEvent"

const socket = mandelboxSuccess.pipe(
  map(([mandelbox]: [MandelboxInfo]) =>
    io(`http://${mandelbox.mandelboxIP}:${mandelbox.mandelboxPorts.port_32261}`, {
      reconnectionDelayMax: 500,
      transports: ["websocket"],
    })
  ),
  share()
)

const socketConnected = socket.pipe(
  switchMap((s: Socket) =>
    fromEvent(s, "connected").pipe(
      filter((clientsConnected: number) => clientsConnected === 2),
      withLatestFrom(of(s)),
      take(1)
    )
  ),
  map(([, s]: [any, Socket]) => s)
)

const socketDisconnected = socket.pipe(
  switchMap((s: Socket) =>
    fromEvent(s, "disconnect").pipe(withLatestFrom(of(s)))
  ),
  map(([, s]: [any, Socket]) => s)
)

const socketReconnectFailed = merge(socket, socketConnected).pipe(
  switchMap((s: Socket) =>
    fromEvent(
      s.io as JQueryStyleEventEmitter<any, Error>,
      "reconnect_error"
    ).pipe(take(15), count())
  )
)


// I'm not sure why this fixes things but it does
socket.subscribe()

export { socket, socketConnected, socketDisconnected, socketReconnectFailed }
