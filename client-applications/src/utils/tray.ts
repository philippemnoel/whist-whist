/**
 * Copyright 2021 Fractal Computers, Inc., dba Whist
 * @file auth.ts
 * @brief This file contains utility functions for managing the MacOS tray, which appears at the
 * top right corner of the screen when the protocol launches.
 */

import events from "events"
import { Menu, Tray, nativeImage } from "electron"

import { trayIconPath } from "@app/config/files"
import { AWSRegion } from "@app/@types/aws"
import { defaultAllowedRegions, allowPayments } from "@app/utils/constants"
import { MenuItem } from "electron/main"
import { persistGet } from "./persist"
import { createSpeedtestWindow } from "@app/utils/windows"

// We create the tray here so that it persists throughout the application
let tray: Tray | null = null
export const trayEvent = new events.EventEmitter()

const createNativeImage = () => {
  const image = nativeImage?.createFromPath(trayIconPath)?.resize({ width: 14 })
  image.setTemplateImage(true)
  return image
}

const feedbackMenu = new MenuItem({
  label: "Support",
  submenu: [
    {
      label: "Feedback",
      click: () => {
        trayEvent.emit("feedback")
      },
    },
    {
      label: "Bug Report",
      click: () => {
        trayEvent.emit("bug")
      },
    },
    {
      label: "Test My Internet Speed",
      click: () => {
        createSpeedtestWindow()
      },
    },
  ],
})

const regionMenu = new MenuItem({
  label: "(Admin Only)",
  submenu: Object.values(defaultAllowedRegions).map((region: AWSRegion) => ({
    label: region,
    type: "radio",
    click: () => {
      trayEvent.emit("region", region)
    },
  })),
})

const accountMenu = new MenuItem({
  label: "Account",
  submenu: [
    ...(allowPayments
      ? [
          {
            label: "Billing Information",
            click: () => {
              trayEvent.emit("payment")
            },
          },
        ]
      : []),
    {
      label: "Sign Out",
      click: () => {
        trayEvent.emit("signout")
      },
    },
  ],
})

const settingsMenu = new MenuItem({
  label: "Settings",
  submenu: [
    {
      label: "Automatically launch on computer start",
      type: "checkbox",
      checked: <boolean>persistGet("autoLaunch", "data"),
      click: () => {
        trayEvent.emit("auto-launch")
      },
    },
    {
      label: "Restore the last browser session",
      type: "checkbox",
      checked: <boolean>persistGet("RestoreLastBrowserSession", "data"),
      click: () => {
        trayEvent.emit("restore-last-browser-session")
      },
    },
    {
      label: "(Coming Soon) Make Whist my default browser",
      click: () => {
        trayEvent.emit("default-browser")
      },
    },
  ],
})

export const destroyTray = () => {
  tray?.destroy()
}

export const createTray = (menu: Menu) => {
  // We should only have one tray at any given time
  if (tray != null) destroyTray()
  // Set the tray icon
  tray = new Tray(createNativeImage())
  // Create the contents of the tray (i.e. menu)
  tray.setContextMenu(menu)
}

export const createMenu = (signedIn: boolean, userEmail?: string) =>
  Menu.buildFromTemplate([
    ...(signedIn ? [accountMenu] : []),
    ...[settingsMenu, feedbackMenu],
    ...(signedIn && (userEmail ?? "").endsWith("@whist.com")
      ? [regionMenu]
      : []),
  ])
