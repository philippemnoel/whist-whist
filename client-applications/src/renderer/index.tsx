/**
 * Copyright Fractal Computers, Inc. 2021
 * @file index.tsx
 * @brief This file is the entry point of the renderer thread and acts as a router.
 */

import React, { useEffect } from "react"
import ReactDOM from "react-dom"

import { OneButtonError, TwoButtonError } from "@app/renderer/pages/error"
import Signout from "@app/renderer/pages/signout"
import Typeform from "@app/renderer/pages/typeform"
import Loading from "@app/renderer/pages/loading"
import Update from "@app/renderer/pages/update"

import {
  WindowHashSignout,
  WindowHashExitTypeform,
  WindowHashBugTypeform,
  WindowHashOnboardingTypeform,
  WindowHashLoading,
  WindowHashUpdate,
  allowPayments,
} from "@app/utils/constants"
import {
  fractalError,
  NO_PAYMENT_ERROR,
  UNAUTHORIZED_ERROR,
  AUTH_ERROR,
  NAVIGATION_ERROR,
} from "@app/utils/error"
import { useMainState } from "@app/utils/ipc"
import TRIGGER from "@app/utils/triggers"

// Electron has no way to pass data to a newly launched browser
// window. To avoid having to maintain multiple .html files for
// each kind of window, we pass a constant across to the renderer
// thread as a query parameter to determine which component
// should be rendered.

// If no query parameter match is found, we default to a
// generic navigation error window.
const show = window.location.search.split("show=")[1]

const RootComponent = () => {
  const [mainState, setMainState] = useMainState()
  const relaunch = () =>
    setMainState({
      trigger: { name: TRIGGER.relaunchAction, payload: undefined },
    })

  const showPaymentWindow = () =>
    setMainState({
      trigger: { name: TRIGGER.showPaymentWindow, payload: undefined },
    })

  const handleSignout = (clearConfig: boolean) =>
    setMainState({
      trigger: { name: TRIGGER.clearCacheAction, payload: { clearConfig } },
    })

  const handleExitTypeform = () =>
    setMainState({
      trigger: { name: TRIGGER.exitTypeformSubmitted, payload: undefined },
    })

  const handleOnboardingTypeform = () =>
    setMainState({
      trigger: {
        name: TRIGGER.onboardingTypeformSubmitted,
        payload: undefined,
      },
    })

  const showSignoutWindow = () =>
    setMainState({
      trigger: { name: TRIGGER.showSignoutWindow, payload: undefined },
    })

  useEffect(() => {
    // We need to ask the main thread to re-emit the current StateIPC because
    // useMainState() only subscribes to state updates after the function is
    // called
    setMainState({
      trigger: { name: TRIGGER.emitIPC, payload: undefined },
    })
  }, [])

  if (show === WindowHashSignout) return <Signout onClick={handleSignout} />
  if (show === WindowHashLoading) return <Loading />
  if (show === WindowHashUpdate) return <Update />
  if (show === WindowHashExitTypeform)
    return (
      <Typeform
        onSubmit={handleExitTypeform}
        id={
          (mainState.appEnvironment ?? "prod") === "prod"
            ? "Yfs4GkeN"
            : "nRa1zGFa"
        }
        email={mainState.userEmail}
      />
    )
  if (show === WindowHashOnboardingTypeform) {
    return (
      <Typeform
        onSubmit={handleOnboardingTypeform}
        id="Oi21wwbg"
        email={mainState.userEmail}
      />
    )
  }
  if (show === WindowHashBugTypeform)
    return (
      <Typeform onSubmit={() => {}} id="VMWBFgGc" email={mainState.userEmail} />
    )
  if (show === NO_PAYMENT_ERROR && allowPayments)
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
