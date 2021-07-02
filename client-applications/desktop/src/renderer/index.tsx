/**
 * Copyright Fractal Computers, Inc. 2021
 * @file index.tsx
 * @brief This file is the entry point of the renderer thread and acts as a router.
 */

import React from "react"
import { chain, keys } from "lodash"
import ReactDOM from "react-dom"

import Update from "@app/renderer/pages/update"
import Error from "@app/renderer/pages/error"
import Signout from "@app/renderer/pages/signout"

import { WindowHashUpdate, WindowHashSignout } from "@app/utils/constants"
import { fractalError } from "@app/utils/error"
import { useMainState } from "@app/utils/ipc"
import TRIGGER from "@app/utils/triggers"

// Electron has no way to pass data to a newly launched browser
// window. To avoid having to maintain multiple .html files for
// each kind of window, we pass a constant across to the renderer
// thread as a query parameter to determine which component
// should be rendered.

// If no query parameter match is found, we default to a
// generic navigation error window.
const show = chain(window.location.search.substring(1))
  .split("=")
  .chunk(2)
  .fromPairs()
  .get("show")
  .value()

const RootComponent = () => {
  const [, setMainState] = useMainState()
  const clearCache = () =>
    setMainState({ trigger: { name: TRIGGER.clearCacheAction, payload: null } })

  if (show === WindowHashUpdate) return <Update />
  if (show === WindowHashSignout) return <Signout onClick={clearCache} />
  if (keys(fractalError).includes(show))
    return (
      <Error
        title={fractalError[show].title}
        text={fractalError[show].text}
        primaryButtonText={fractalError[show].primaryButton}
        secondaryButtonText={fractalError[show].secondaryButton}
      />
    )
  return (
    <Error
      title={fractalError.NAVIGATION_ERROR.title}
      text={fractalError.NAVIGATION_ERROR.text}
      primaryButtonText={fractalError.NAVIGATION_ERROR.primaryButton}
      secondaryButtonText={fractalError.NAVIGATION_ERROR.secondaryButton}
    />
  )
}

// TODO: actually pass version number through IPC.
const WindowBackground = (props: any) => {
  return <div className="relative w-full h-full bg-white">{props.children}</div>
}

ReactDOM.render(
  <WindowBackground>
    <RootComponent />
  </WindowBackground>,
  document.getElementById("root")
)
