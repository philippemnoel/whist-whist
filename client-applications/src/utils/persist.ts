/**
 * Copyright 2021 Fractal Computers, Inc., dba Whist
 * @file persist.ts
 * @brief This file contains utility functions for interacting with electron-store, which is how
 * we persist data across sessions. This file manages the low-level details of how we'll persist
 * user data in between Electron sessions. From the perspective of the rest of the app, we should
 * just able to call these functions and not worry about any of the implementation details.
 * Persistence requires access to the file system, so it can only be used by the main process. Only
 * serializable data can be persisted. Essentially, anything that can be converted to JSON.
 */

import { app } from "electron"
import Store from "electron-store"
import { loggingBaseFilePath } from "@app/config/environment"

app.setPath("userData", loggingBaseFilePath)

export const store = new Store({ watch: true })

interface Cache {
  [k: string]: string | boolean
}

type CacheName = "auth" | "data"

const persistedAuth = store.get("auth") as Cache

const persist = (key: string, value: string | boolean, cache?: CacheName) => {
  store.set(`${cache ?? "auth"}.${key}`, value)
}

const persistClear = (keys: Array<keyof Cache>, cache: CacheName) => {
  keys.forEach((key) => {
    store.delete(`${cache as string}.${key as string}`)
  })
}

const persistGet = (key: keyof Cache, cache?: CacheName) =>
  (store.get(cache ?? "auth") as Cache)?.[key]

export { Cache, persist, persistedAuth, persistClear, persistGet }
