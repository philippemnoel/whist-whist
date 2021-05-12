/**
 * Copyright Fractal Computers, Inc. 2021
 * @file ipc.ts
 * @brief This file contains all RXJS observables created from IPC event emitters.
 */

import { ipcMain } from "electron"
import { fromEvent } from "rxjs"
import { get, identity } from "lodash"
import { map, share, startWith, filter } from "rxjs/operators"
import { StateIPC } from "@app/@types/state"
import { Object } from "ts-toolbelt"

import { StateChannel } from "@app/utils/constants"
import { trigger } from "@app/main/utils/flows"

// This file listens for incoming messages on the single Electron IPC channel
// that our app uses to communicate with renderer processes. Messages are sent
// as a object that represents the new "state" of the system.
//
// This object will be populated with recent input from the renderer thread, and
// it might have a shape like so:
//
// {
//     email: ...,
//     password: ...,
//     loginRequest: [...],
// }
//
// Downstream observables will subscribe to the individual keys of this object,
// and react when they receive new state.

// Once again, rxjs and Typescript do not agree about type inference, so we
// manually coerce.

// It's important that we manually startWith a value here. We need an initial
// state object to emit at the beginning of the application so downstream
// observables can initialize.

// export const eventIPC = trigger(
//   "eventIPC",
//   fromEvent(ipcMain, StateChannel).pipe(
//     map((args) => {
//       if (!Array.isArray(args)) return {} as Partial<StateIPC>
//       return args[1] as Partial<StateIPC>
//     }),
//     startWith({}),
//     share()
//   )
// )

// export const fromEventIPC = <T extends Object.Paths<StateIPC>>(...keys: T) =>
//   trigger(
//     "fromEventIPC",
//     eventIPC.pipe(
//       map((obj) => get(obj as Partial<StateIPC>, keys)),
//       filter(identity),
//       map((obj) => obj as NonNullable<Object.Path<StateIPC, T>>),
//       share()
//     )
//   )
