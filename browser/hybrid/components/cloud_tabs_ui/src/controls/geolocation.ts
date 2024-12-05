import { getTabId } from "./session"
import { cloneAsObject } from "./utils"

const geolocationSuccessCallback = (metaTagName: string, tabId: number) => {
  return (position: GeolocationPosition) => {
    // Send message back to server with position
    ;(chrome as any).whist.broadcastWhistMessage(
      JSON.stringify({
        type: "GEOLOCATION_REQUEST_COMPLETED",
        value: {
          success: true,
          response: cloneAsObject(position),
          metaTagName: metaTagName,
          tabId: tabId
        },
      })
    )
  }
}

const geolocationErrorCallback = (metaTagName: string, tabId: number) => {
  return (positionError: GeolocationPositionError) => {
    // Send message back to server with failure
    ;(chrome as any).whist.broadcastWhistMessage(
      JSON.stringify({
        type: "GEOLOCATION_REQUEST_COMPLETED",
        value: {
          success: false,
          response: cloneAsObject(positionError),
          metaTagName: metaTagName,
          tabId: tabId
        },
      })
    )
  }
}

const intializeGeolocationRequestHandler = () => {
  ;(chrome as any).whist.onMessage.addListener((message: string) => {
    const parsed = JSON.parse(message)

    if (
      parsed.type === "GEOLOCATION_REQUESTED" &&
      parsed.value.id === getTabId()
    ) {
      const requestedFunction = parsed.value.params.function
      const serverMetaTagName = parsed.value.metaTagName
      switch(requestedFunction) {
        case 'getCurrentPosition':
          navigator.geolocation.getCurrentPosition(
            geolocationSuccessCallback(serverMetaTagName, parsed.value.id),
            geolocationErrorCallback(serverMetaTagName, parsed.value.id),
            parsed.value.options
          )
          break;

        case 'watchPosition':
          const watchHandle = navigator.geolocation.watchPosition(
            geolocationSuccessCallback(serverMetaTagName, parsed.value.id),
            geolocationErrorCallback(serverMetaTagName, parsed.value.id),
            parsed.value.options
          )
          // Listen for an event for clearing this specific watch
          ;(chrome as any).whist.onMessage.addListener((message: string) => {
            const clearWatchParsed = JSON.parse(message)

            if (
              clearWatchParsed.type === "GEOLOCATION_REQUESTED" &&
              clearWatchParsed.value.id === getTabId() &&
              clearWatchParsed.value.params.function === "clearWatch" &&
              clearWatchParsed.value.metaTagName === serverMetaTagName
            ) {
              navigator.geolocation.clearWatch(watchHandle)
            }
          })
          break;
      }
    }
  })
}

export {
  intializeGeolocationRequestHandler,
}
