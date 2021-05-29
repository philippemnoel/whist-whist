// This file is home to observables that manage launching the protocol process
// on the user's machine. They are distinct from the observables that
// communicate with the webserver to manage mandelbox creation.
//
// Many of these observables emit the protocol ChildProcess object, which
// carries important data about the state of the protocol process.

import { ChildProcess } from "child_process"
import { flow, fork, createTrigger } from "@app/utils/flows"

export default flow<ChildProcess>("protocolCloseFlow", (trigger) => {
  const close = fork<ChildProcess>(trigger, {
    success: (protocol) => !protocol?.killed || protocol === undefined,
    failure: (protocol) => protocol?.killed,
  })

  return {
    success: createTrigger("protocolCloseFlowSuccess", close.success),
    failure: createTrigger("protocolCloseFlowFailure", close.failure),
  }
})
