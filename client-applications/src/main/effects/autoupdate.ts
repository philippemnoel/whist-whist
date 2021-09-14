import { app } from "electron"
import { autoUpdater } from "electron-updater"
import { take, takeUntil } from "rxjs/operators"

import { appEnvironment, FractalEnvironments } from "../../../config/configs"
import { fromTrigger } from "@app/utils/flows"
import { createUpdateWindow } from "@app/utils/windows"
import { protocolStreamKill } from "@app/utils/protocol"
import { closeAllWindows } from "@app/utils/windows"
import { logBase } from "@app/utils/logging"

// Apply autoupdate config
fromTrigger("appReady")
  .pipe(take(1), takeUntil(fromTrigger("updateChecking")))
  .subscribe(() => {
    // We want to manually control when we download the update via autoUpdater.quitAndInstall(),
    // so we need to set autoDownload = false
    autoUpdater.autoDownload = false 
    // In dev and staging, the file containing the version is called {channel}-mac.yml, so we need to set the
    // channel down below. In prod, the file is called latest-mac.yml, which channel defaults to, so
    // we don't need to set it.
    switch (appEnvironment) {
      case FractalEnvironments.STAGING:
        autoUpdater.channel = "staging-rc"
        break
      case FractalEnvironments.DEVELOPMENT:
        autoUpdater.channel = "dev-rc"
        break
      default:
        autoUpdater.channel = "dev-rc"
        break
    }

    // This is what looks for a latest.yml file in the S3 bucket in electron-builder.config.js,
    // and fires an update if the current version is less than the version in latest.yml
    autoUpdater.checkForUpdatesAndNotify().catch((err) => console.error(err))
  })

fromTrigger("updateDownloaded").pipe(take(1)).subscribe(() => {
  createUpdateWindow()
})

fromTrigger("installUpdate").subscribe(() => {
  autoUpdater.quitAndInstall()
})
