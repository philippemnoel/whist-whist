/**
 * Copyright 2021 Whist Technologies, Inc.
 * @file index.tsx
 * @brief This file is the entry point of the renderer thread and acts as a router.
 */

import React, { useEffect, useState } from "react"
import ReactDOM from "react-dom"

import { OneButtonError, TwoButtonError } from "@app/renderer/pages/error"
import Signout from "@app/renderer/pages/signout"
import Typeform from "@app/renderer/pages/typeform"
import Importer from "@app/renderer/pages/importer"
import Update from "@app/renderer/pages/update"
import Network from "@app/renderer/pages/network"

import {
  WindowHashSignout,
  WindowHashBugTypeform,
  WindowHashUpdate,
  WindowHashImporter,
  WindowHashOnboarding,
} from "@app/constants/windows"
import {
  fractalError,
  NO_PAYMENT_ERROR,
  UNAUTHORIZED_ERROR,
  AUTH_ERROR,
  NAVIGATION_ERROR,
} from "@app/utils/error"
import { useMainState } from "@app/utils/ipc"
import { WhistTrigger } from "@app/constants/triggers"

// Electron has no way to pass data to a newly launched browser
// window. To avoid having to maintain multiple .html files for
// each kind of window, we pass a constant across to the renderer
// thread as a query parameter to determine which component
// should be rendered.

const RootComponent = () => {
  const [show, setShow] = useState(window.location.search.split("show=")[1])
  const [mainState, setMainState] = useMainState()

  const relaunch = () =>
    setMainState({
      trigger: { name: WhistTrigger.relaunchAction, payload: undefined },
    })

  const showPaymentWindow = () =>
    setMainState({
      trigger: { name: WhistTrigger.showPaymentWindow, payload: undefined },
    })

  const handleSignout = (clearConfig: boolean) =>
    setMainState({
      trigger: {
        name: WhistTrigger.clearCacheAction,
        payload: { clearConfig },
      },
    })

  const showSignoutWindow = () =>
    setMainState({
      trigger: { name: WhistTrigger.showSignoutWindow, payload: undefined },
    })

  const handleImporterSubmit = (browser: string | undefined) => {
    setMainState({
      trigger: {
        name: WhistTrigger.onboarded,
        payload: { importBrowserDataFrom: browser },
      },
    })
  }

  const handleNetworkSubmit = () => setShow(WindowHashImporter)

  useEffect(() => {
    // We need to ask the main thread to re-emit the current StateIPC because
    // useMainState() only subscribes to state updates after the function is
    // called
    setMainState({
      trigger: { name: WhistTrigger.emitIPC, payload: undefined },
    })
  }, [])

  if (show === WindowHashSignout) return <Signout onClick={handleSignout} />
  if (show === WindowHashUpdate) return <Update />
  if (show === WindowHashImporter)
    return (
      <Importer
        browsers={mainState.browsers ?? []}
        onSubmit={(browser: string | undefined) =>
          handleImporterSubmit(browser)
        }
      />
    )
  if (show === WindowHashOnboarding) {
    return (
      <Network
        networkInfo={mainState.networkInfo}
        onSubmit={handleNetworkSubmit}
      />
    )
  }
  if (show === WindowHashBugTypeform)
    return (
      <Typeform onSubmit={() => {}} id="VMWBFgGc" email={mainState.userEmail} />
    )
  if (show === NO_PAYMENT_ERROR)
    return (
      <TwoButtonError
        title={fractalError[show].title}
        text={fractalError[show].text}
        primaryButtonText="Update Payment"
        secondaryButtonText="Sign Out"
        onPrimaryClick={showPaymentWindow}
        onSecondaryClick={showSignoutWindow}
      />
    )
  if ([UNAUTHORIZED_ERROR, AUTH_ERROR, NAVIGATION_ERROR].includes(show))
    return (
      <OneButtonError
        title={fractalError[show].title}
        text={fractalError[show].text}
        primaryButtonText="Sign Out"
        onPrimaryClick={showSignoutWindow}
      />
    )
  if (Object.keys(fractalError).includes(show))
    return (
      <TwoButtonError
        title={fractalError[show].title}
        text={fractalError[show].text}
        primaryButtonText="Try Again"
        secondaryButtonText="Sign Out"
        onPrimaryClick={relaunch}
        onSecondaryClick={showSignoutWindow}
      />
    )
  return (
    <TwoButtonError
      title={fractalError.NAVIGATION_ERROR.title}
      text={fractalError.NAVIGATION_ERROR.text}
      primaryButtonText="Try Again"
      secondaryButtonText="Sign Out"
      onPrimaryClick={relaunch}
      onSecondaryClick={showSignoutWindow}
    />
  )
}

// TODO: actually pass version number through IPC.
const WindowBackground = (props: any) => {
  return (
    <div className="relative w-full h-full bg-opacity-0">{props.children}</div>
  )
}

ReactDOM.render(
  <WindowBackground>
    <RootComponent />
  </WindowBackground>,
  document.getElementById("root")
)
