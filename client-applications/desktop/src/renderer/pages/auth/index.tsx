import React, { useState } from "react"
import { Route } from "react-router-dom"

import Login from "@app/renderer/pages/auth/login"
import Signup from "@app/renderer/pages/auth/signup"
import { useMainState } from "@app/utils/ipc"

const Auth = () => {
  /*
        Description:
            Router for auth-related pages (e.g. login and signup)
    */

  const [mainState, setMainState] = useMainState()
  const [email, setEmail] = useState("")
  const [password, setPassword] = useState("")
  const [confirmPassword, setConfirmPassword] = useState("")

  const clearPassword = () => {
    setPassword("")
    setConfirmPassword("")
  }

  const onLogin = () => {
    setMainState({
      email,
      trigger: {
        name: "login",
        payload: {
          email,
          password,
        },
      },
    })
  }

  const onSignup = () => {
    setMainState({
      email,
      trigger: {
        name: "signup",
        payload: {
          email,
          password,
        },
      },
    })
  }

  return (
    <>
      <Route
        exact
        path="/login"
        render={() => (
          <Login
            email={email}
            password={password}
            warning={mainState?.loginWarning}
            loading={mainState?.loginLoading}
            onLogin={onLogin}
            onNavigate={clearPassword}
            onChangeEmail={setEmail}
            onChangePassword={setPassword}
          />
        )}
      />
      <Route
        exact
        path="/"
        render={() => (
          <Signup
            email={email}
            password={password}
            confirmPassword={confirmPassword}
            warning={mainState?.signupWarning}
            loading={mainState?.signupLoading}
            onSignup={onSignup}
            onNavigate={clearPassword}
            onChangeEmail={setEmail}
            onChangePassword={setPassword}
            onChangeConfirmPassword={setConfirmPassword}
          />
        )}
      />
    </>
  )
}

export default Auth
