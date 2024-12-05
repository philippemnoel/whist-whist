import {
  ContentScriptMessage,
  ContentScriptMessageType,
} from "@app/constants/ipc"

const getUniqueId = () => {
  // This should always be an integer
  return Date.now()
}

const setMetaGeolocationTagFinished = (metaGeolocationTag: HTMLMetaElement) => {
  metaGeolocationTag.content = JSON.stringify({deleteTag: true})
}

const geolocationPositionFunction = (
  functionName: string,
  successCallback: PositionCallback,
  errorCallback: PositionErrorCallback | null | undefined,
  options: any
) => {
  const uniqueId = getUniqueId()

  const metaGeolocation = document.createElement("meta")
  metaGeolocation.name = `${uniqueId}-geolocation`
  metaGeolocation.content = JSON.stringify({
    function: functionName,
    options: options,
  })

  // Listen for changes to content of the meta tag to get response
  const metaGeolocationObserver = new MutationObserver((mutationList, observer) => {
    const metaTagContentJSON = JSON.parse(metaGeolocation.content)

    // If contents say deleteTag, then remove observer and remove tag
    if (metaTagContentJSON.deleteTag) {
      observer.disconnect()
      document.documentElement.removeChild(metaGeolocation)
      return
    }

    if ('success' in metaTagContentJSON) {
      if (metaTagContentJSON.success) {
        // success true means geolocation request returned a GeolocationPosition
        successCallback(metaTagContentJSON.response as GeolocationPosition)
      } else if (!metaTagContentJSON.success && errorCallback) {
        errorCallback(metaTagContentJSON.response as GeolocationPositionError)
      }

      // If a state of success has been determined, and the calling function was
      // getCurrentPosition, then clear the observer

      setMetaGeolocationTagFinished(metaGeolocation)
    }
  })
  metaGeolocationObserver.observe(metaGeolocation, {
    attributes: true,
    attributeFilter: [ "content" ]
  })

  document.documentElement.appendChild(metaGeolocation)

  return uniqueId
}

navigator.geolocation.getCurrentPosition = (
  successCallback,
  errorCallback,
  options
) => {
  geolocationPositionFunction("getCurrentPosition", successCallback, errorCallback, options)
}

navigator.geolocation.watchPosition = (
  successCallback,
  errorCallback,
  options
) => {
  // Use the unique ID as the handler number for reference in `clearWatch`
  const handlerId = geolocationPositionFunction("watchPosition", successCallback, errorCallback, options)

  return handlerId
}

navigator.geolocation.clearWatch = (id) => {
  const metaGeolocationTag = document.documentElement.querySelector(
    `meta[name="${id}-geolocation"]`
  ) as HTMLMetaElement

  if (metaGeolocationTag) {
    setMetaGeolocationTagFinished(metaGeolocationTag)
  }

  // Need to send message to extension since we're not creating
  // a new meta tag and so the content script won't catch this
  chrome.runtime.sendMessage(<ContentScriptMessage>{
    type: ContentScriptMessageType.GEOLOCATION_REQUEST,
    value: {
      params: {function: "clearWatch"},
      metaTagName: metaGeolocationTag.name
    },
  })
}
