import { socket } from "@app/worker/events/socketio"
import { geolocationRequested, geolocationRequestCompleted } from "@app/worker/events/geolocation"
import { getTab } from "@app/worker/utils/tabs"

import { withLatestFrom } from "rxjs/operators"
import { Socket } from "socket.io-client"

geolocationRequested
  .pipe(withLatestFrom(socket))
  .subscribe(
    ([[requestParams, metaTagName, tabId], socket]: [
      [any, string, number],
      Socket
    ]) => {
      ;(chrome as any).whist.broadcastWhistMessage(
        JSON.stringify({
          type: "GEOLOCATION_REQUESTED",
          value: {
            id: tabId,
            metaTagName: metaTagName,
            params: requestParams
          },
        })
      )
    }
  )

geolocationRequestCompleted
  .pipe(withLatestFrom(socket))
  .subscribe(
    ([[success, response, metaTagName, tabId], socket]: [
      [boolean, any, string, number],
      Socket
    ]) => {
      socket.emit("geolocation-request-completed", success, response, metaTagName, tabId)
    }
  )
