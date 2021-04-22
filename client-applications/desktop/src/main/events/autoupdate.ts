import { autoUpdater } from 'electron-updater'
import EventEmitter from 'events'
import { fromEvent } from 'rxjs'

export const eventUpdateAvailable = fromEvent(
    autoUpdater as EventEmitter,
    'update-available'
)

export const eventUpdateNotAvailable = fromEvent(
    autoUpdater as EventEmitter,
    'update-not-available'
)

export const eventDownloadProgress = fromEvent(
    autoUpdater as EventEmitter,
    'download-progress'
)

export const eventUpdateDownloaded = fromEvent(
    autoUpdater as EventEmitter,
    'update-downloaded'
)
