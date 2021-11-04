/**
 * Copyright 2021 Fractal Computers, Inc., dba Whist
 * @file tray.ts
 * @brief This file contains all RXJS observables created from events caused by clicking on the tray.
 */
import { fromEvent } from "rxjs"

import { trayEvent } from "@app/utils/tray"
import { createTrigger } from "@app/utils/flows"
import TRIGGER from "@app/utils/triggers"

createTrigger(TRIGGER.showPaymentWindow, fromEvent(trayEvent, "payment"))
createTrigger(TRIGGER.showSignoutWindow, fromEvent(trayEvent, "signout"))
createTrigger(TRIGGER.trayFeedbackAction, fromEvent(trayEvent, "feedback"))
createTrigger(TRIGGER.trayRegionAction, fromEvent(trayEvent, "region"))
createTrigger(TRIGGER.trayBugAction, fromEvent(trayEvent, "bug"))
createTrigger(TRIGGER.trayAutolaunchAction, fromEvent(trayEvent, "auto-launch"))
createTrigger(
  TRIGGER.trayRestoreSessionAction,
  fromEvent(trayEvent, "restore-last-chrome-session")
)
