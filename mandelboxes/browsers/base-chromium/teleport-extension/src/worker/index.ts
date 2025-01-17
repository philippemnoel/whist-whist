/*
    The entry point to our service worker.
    A service worker is Javascript that runs in the background of Chrome.
    For more info, see https://developer.chrome.com/docs/workbox/service-worker-overview/
*/

import { initFileSyncHandler } from "./downloads"
import { initNativeHostIpc, initNativeHostDisconnectHandler } from "./ipc"
import { initCursorLockHandler } from "./cursor"
import { initLocationHandler } from "./geolocation"
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

// Receive geolocation from extension host
initLocationHandler(nativeHostPort)

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
