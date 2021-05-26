import events from "events"
import { Menu, Tray, nativeImage } from "electron"
import { values } from "lodash"

import { trayIconPath } from "@app/config/files"
import { AWSRegion, defaultAllowedRegions } from "@app/@types/aws"
import { MenuItem } from "electron/main"

// We create the tray here so that it persists throughout the application
let tray: Tray | null = null
export const trayEvent = new events.EventEmitter()

const createNativeImage = () => {
  let image = nativeImage.createFromPath(trayIconPath)
  image = image.resize({ width: 16 })
  image.setTemplateImage(true)
  return image
}

const rootMenu = [
  {
    label: "Sign out",
    click: () => {
      trayEvent.emit("signout")
    },
  },
  {
    label: "Quit",
    click: () => {
      trayEvent.emit("quit")
    },
  },
]

const regionMenu = [
  {
    label: "Region",
    submenu: values(defaultAllowedRegions).map((region: AWSRegion) => ({
      label: region,
      type: "radio",
      click: () => {
        trayEvent.emit("region", region)
      },
    })),
  } as unknown as MenuItem,
]

export const createTray = (email: string) => {
  // We should only have one tray at any given time
  if (tray != null) tray.destroy()

  tray = new Tray(createNativeImage())
  // If the user is a dev user, then allow them to toggle regions for testing
  const template = email.includes("fractal.co")
    ? [...rootMenu, ...regionMenu]
    : [...rootMenu]
  const menu = Menu.buildFromTemplate(template)
  tray.setContextMenu(menu)
}
