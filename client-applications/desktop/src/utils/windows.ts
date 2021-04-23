import path from "path"
import { app, BrowserWindow, BrowserWindowConstructorOptions } from "electron"
import {
  WindowHashAuth,
  WindowHashUpdate,
  WindowHashAuthError,
  WindowHashProtocolError,
  WindowHashCreateContainerErrorNoAccess,
  WindowHashCreateContainerErrorUnauthorized,
  WindowHashCreateContainerErrorInternal,
  WindowHashAssignContainerError,
} from "@app/utils/constants"
import config, { FractalCIEnvironment } from "@app/config/environment"

const { buildRoot } = config

const base = {
  webPreferences: { preload: path.join(buildRoot, "preload.js") },
  resizable: false,
  titleBarStyle: "hidden",
}

const width = {
  xs: { width: 16 * 24 },
  sm: { width: 16 * 32 },
  md: { width: 16 * 40 },
  lg: { width: 16 * 56 },
  xl: { width: 16 * 64 },
  xl2: { width: 16 * 80 },
  xl3: { width: 16 * 96 },
}

const height = {
  xs: { height: 16 * 20 },
  sm: { height: 16 * 32 },
  md: { height: 16 * 40 },
  lg: { height: 16 * 56 },
  xl: { height: 16 * 64 },
  xl2: { height: 16 * 80 },
  xl3: { height: 16 * 96 },
}

type CreateWindowFunction = (
  onReady?: (win: BrowserWindow) => any,
  onClose?: (win: BrowserWindow) => any
) => BrowserWindow

export const getWindows = () => BrowserWindow.getAllWindows()

export const closeWindows = () => {
  BrowserWindow.getAllWindows().forEach((win) => win.close())
}

export const showAppDock = () => {
  // On non-macOS systems, app.dock is null, so we
  // do nothing here.
  app?.dock?.show().catch((err) => console.error(err))
}

export const hideAppDock = () => {
  // On non-macOS systems, app.dock is null, so we
  // do nothing here.
  app?.dock?.hide()
}

export const getWindowTitle = () => {
  const { deployEnv } = config
  if (deployEnv === FractalCIEnvironment.PRODUCTION) {
    return "Fractal"
  }
  return `Fractal (${deployEnv})`
}

// ability to add in custom params for stripe navigation
const loadWindow = (params: string, win: BrowserWindow) => {
  if (app.isPackaged) {
    win
      .loadFile("build/index.html", { search: params })
      .catch((err) => console.log(err))
  } else {
    win
      .loadURL("http://localhost:8080" + params)
      .then(() => {
        win.webContents.openDevTools({ mode: "undocked" })
      })
      .catch((err) => console.error(err))
  }
}

export const createWindow = (
  show: string,
  options: Partial<BrowserWindowConstructorOptions>,
  onReady?: (win: BrowserWindow) => any,
  onClose?: (win: BrowserWindow) => any
) => {
  const win = new BrowserWindow({ ...options, show: false })

  const params = "?show=" + show

  loadWindow(params, win)

  win.webContents.on("did-finish-load", () =>
    onReady != null ? onReady(win) : win.show()
  )

  win.webContents.on("did-navigate", (event, url) => {
    event.preventDefault()
    /*
      place url handling here for stripe navigation.
      call loadWindow with params
    */
    console.log(url)
    if (url === "https://www.fractal.co/") loadWindow(params, win)
  })

  win.on("close", () => onClose?.(win))

  return win
}

export const createAuthWindow: CreateWindowFunction = () =>
  createWindow(WindowHashAuth, {
    ...base,
    ...width.sm,
    ...height.md,
  } as BrowserWindowConstructorOptions)

export const createAuthErrorWindow: CreateWindowFunction = () =>
  createWindow(WindowHashAuthError, {
    ...base,
    ...width.md,
    ...height.xs,
  } as BrowserWindowConstructorOptions)

export const createContainerErrorWindowNoAccess: CreateWindowFunction = () =>
  createWindow(WindowHashCreateContainerErrorNoAccess, {
    ...base,
    ...width.md,
    ...height.xs,
  } as BrowserWindowConstructorOptions)

export const createContainerErrorWindowUnauthorized: CreateWindowFunction = () =>
  createWindow(WindowHashCreateContainerErrorUnauthorized, {
    ...base,
    ...width.md,
    ...height.xs,
  } as BrowserWindowConstructorOptions)

export const createContainerErrorWindowInternal: CreateWindowFunction = () =>
  createWindow(WindowHashCreateContainerErrorInternal, {
    ...base,
    ...width.md,
    ...height.xs,
  } as BrowserWindowConstructorOptions)

export const assignContainerErrorWindow: CreateWindowFunction = () =>
  createWindow(WindowHashAssignContainerError, {
    ...base,
    ...width.md,
    ...height.xs,
  } as BrowserWindowConstructorOptions)

export const createProtocolErrorWindow: CreateWindowFunction = () =>
  createWindow(WindowHashProtocolError, {
    ...base,
    ...width.md,
    ...height.xs,
  } as BrowserWindowConstructorOptions)

export const createUpdateWindow: CreateWindowFunction = () =>
  createWindow(WindowHashUpdate, {
    ...base,
    ...width.sm,
    ...height.md,
  } as BrowserWindowConstructorOptions)
