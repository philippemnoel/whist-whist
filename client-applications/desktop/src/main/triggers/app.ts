/**
 * Copyright Fractal Computers, Inc. 2021
 * @file app.ts
 * @brief This file contains all RXJS observables created from Electron app event emitters.
 */

import { app } from "electron"
import EventEmitter from "events"
import { fromEvent } from "rxjs"

import { createTrigger } from "@app/utils/flows"

// Fires when Electron starts; this is the first event to fire
createTrigger("appReady", fromEvent(app as EventEmitter, "ready"))
// Fires whenever a BrowserWindow is created
createTrigger(
  "windowCreated",
  fromEvent(app as EventEmitter, "browser-window-created")
)
