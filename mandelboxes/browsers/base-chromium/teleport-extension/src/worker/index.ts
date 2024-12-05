/*
    The entry point to our service worker.
    A service worker is Javascript that runs in the background of Chrome.
    For more info, see https://developer.chrome.com/docs/workbox/service-worker-overview/
*/

import { initFileSyncHandler } from "./downloads"
import { initNativeHostIpc, initNativeHostDisconnectHandler } from "./ipc"
import { initCursorLockHandler } from "./cursor"
import { initLocationHandler } from "./geolocation"
import { initKeyboardRepeatRateHandler } from "./keyboard"
import { initLanguageInitHandler, initLanguageChangeHandler } from "./language"
import { 
  initPreferencesInitHandler, 
  initDarkModeChangeHandler,
  initTimezoneChangeHandler,
} from "./preferences"
import {
  initTabState,
  initSocketioConnection,
  initActivateTabListener,
  initCloseTabListener,
  initCloudTabUpdatedListener,
  initCloudTabCreatedListener,
  initTabRefreshListener,
} from "./tabs"
import {
  initAddCookieListener,
  initCookieAddedListener,
  initRemoveCookieListener,
  initCookieRemovedListener,
  initCookieSyncHandler,
} from "./cookies"
import { initZoomListener } from "./zoom"
import { initWindowCreatedListener } from "./windows"

initTabState()

const socket = initSocketioConnection()
const nativeHostPort = initNativeHostIpc()

// Disconnects the host native port on command
initNativeHostDisconnectHandler(nativeHostPort)

// Enables relative mouse mode
initCursorLockHandler(nativeHostPort)

// Send and receive geolocation actions
initLocationHandler(socket)

// Receive keyboard repeat rate changes
initKeyboardRepeatRateHandler(socket, nativeHostPort)

// Send and receive language actions
initLanguageInitHandler(socket)
initLanguageChangeHandler(socket)

// Send and receive preferences actions
initPreferencesInitHandler(socket)
initDarkModeChangeHandler(socket)
initTimezoneChangeHandler(socket, nativeHostPort)

// Listen to the client for tab actions
initActivateTabListener(socket)
initCookieSyncHandler(socket)
initCloseTabListener(socket)
initCloudTabUpdatedListener(socket)
initCloudTabCreatedListener(socket)
initAddCookieListener(socket)
initRemoveCookieListener(socket)
initCookieAddedListener(socket)
initCookieRemovedListener(socket)
initTabRefreshListener(socket)
initZoomListener(socket)

// Initialize the file upload/download handler
initFileSyncHandler(socket)

// Listen for newly created windows
initWindowCreatedListener(socket)
