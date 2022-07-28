/*
    The entry point to our service worker.
    A service worker is Javascript that runs in the background of Chrome.
    For more info, see https://developer.chrome.com/docs/workbox/service-worker-overview/
*/

import { initFileSyncHandler } from "./downloads"
import { initChromeWelcomeRedirect } from "./navigation"
import { initNativeHostIpc, initNativeHostDisconnectHandler } from "./ipc"
import { initCursorLockHandler } from "./cursor"
import { initTabDetachSuppressor, initTabState } from "./tabs"
import { initLocationHandler } from "./geolocation"
import {
  initSocketioConnection,
  initActivateTabListener,
  initCloseTabListener,
  initCloudTabUpdatedListener,
  initCloudTabCreatedListener,
  initHistoryNavigateListener,
  initTabRefreshListener
} from "./socketio"
import {
  initAddCookieListener,
  initCookieAddedListener,
  initRemoveCookieListener,
  initCookieRemovedListener,
} from "./cookies"

initTabState()

const socket = initSocketioConnection()
const nativeHostPort = initNativeHostIpc()

// Disconnects the host native port on command
initNativeHostDisconnectHandler(nativeHostPort)

// Initialize the file upload/download handler
initFileSyncHandler(nativeHostPort)

// Enables relative mouse mode
initCursorLockHandler(nativeHostPort)

// Prevents tabs from being dragged out to new windows
initTabDetachSuppressor()

// Redirects the chrome://welcome page to our own Whist-branded page
initChromeWelcomeRedirect()

// Receive geolocation from extension host
initLocationHandler(nativeHostPort)

// Listen to the client for tab actions
initActivateTabListener(socket)
initCloseTabListener(socket)
initCloudTabUpdatedListener(socket)
initCloudTabCreatedListener(socket)
initAddCookieListener(socket)
initRemoveCookieListener(socket)
initCookieAddedListener(socket)
initCookieRemovedListener(socket)
initTabRefreshListener(socket)
