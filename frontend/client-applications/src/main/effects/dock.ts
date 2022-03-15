/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file app.ts
 * @brief This file contains effects that deal with the Electron app.dock module
 */

import { app } from "electron"

import { fromTrigger } from "@app/main/utils/flows"
import { WhistTrigger } from "@app/constants/triggers"

// Hide the Electron icon when the protocol connects to avoid the double icon issue
fromTrigger(WhistTrigger.mandelboxFlowSuccess).subscribe(() => {
  app?.dock?.hide()
})
