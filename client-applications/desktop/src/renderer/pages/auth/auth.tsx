import React from "react"
import { Route } from "react-router-dom"

import Login from "@app/renderer/pages/auth/login"
import Signup from "@app/renderer/pages/auth/signup"

const Auth = () => {
    /*
        Description:
            Router for auth-related pages (e.g. login and signup)
    */

    const onLogin = () => {
        console.log("Logged in!")
    }

    const onSignup = () => {
        console.log("Signed up!")
    }

    return (
        <>
            <Route exact path="/" render={() => <Login onLogin={onLogin} />} />
            <Route
                path="/auth/login"
                render={() => <Login onLogin={onLogin} />}
            />
            <Route
                path="/auth/signup"
                render={() => <Signup onSignup={onSignup} />}
            />
        </>
    )
}

export default Auth
