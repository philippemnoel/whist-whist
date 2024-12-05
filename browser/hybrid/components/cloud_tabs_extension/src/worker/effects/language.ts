import { withLatestFrom } from "rxjs/operators"
import { Socket } from "socket.io-client"

import { languagesChanged } from "@app/worker/events/language"
import { socket, socketConnected } from "@app/worker/events/socketio"

import { whistState } from "@app/worker/utils/state"

socketConnected.subscribe((socket: Socket) => {
  socket.emit("init-languages", navigator.language, navigator.languages)
})

languagesChanged
  .pipe(withLatestFrom(socket))
  .subscribe(([, socket]: [any, Socket]) => {
    socket.emit("language-changed", navigator.language, navigator.languages)
  })
