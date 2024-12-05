import { initializeSessionStorage } from "./controls/session"
import {
  initializeGetTabId,
  initializeFaviconUpdater,
  initializeTitleUpdater,
} from "./controls/tab"
import {
  initializeURLUpdater,
  initializePopStateHandler,
} from "./controls/history"
import {
  initializeWhistTagHandler,
  initializeWhistFreezeAllHandler,
  initializeWhistSpotlightHandler,
  initializeRequestMandelbox,
  GPUCommandStreaming,
} from "./controls/whist"
import { initializePageRefreshHandler } from "./controls/refresh"
import { initializeMouseEnterHandler } from "./controls/mouse"
import { intializeGeolocationRequestHandler } from "./controls/geolocation"

import {
  createStartNotification,
  createDisconnectNotification,
} from "./ui/notifications"
import { initializeLoadingScreen } from "./ui/loading"

window.onload = async () => {
  initializeSessionStorage()
  const gpu_streaming = await GPUCommandStreaming()
  if (!gpu_streaming) {
    createStartNotification()
    createDisconnectNotification()
  }

  initializeGetTabId().then(() => {
    initializeURLUpdater()
    initializePopStateHandler()
    initializeTitleUpdater()
    initializeFaviconUpdater()
    initializePageRefreshHandler()
    initializeMouseEnterHandler()
  })

  if (!gpu_streaming) {
    initializeLoadingScreen()
  }
  initializeWhistTagHandler()
  initializeWhistFreezeAllHandler()
  initializeWhistSpotlightHandler()
  initializeRequestMandelbox()
  intializeGeolocationRequestHandler()
}
