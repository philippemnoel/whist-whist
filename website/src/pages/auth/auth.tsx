import React, { useEffect } from "react"
import { connect } from "react-redux"
import { Redirect } from "react-router"

import Header from "shared/components/header"
import LoginView from "pages/auth/views/loginView"
import SignupView from "pages/auth/views/signupView"
import ForgotView from "pages/auth/views/forgotView"

import "styles/auth.css"

import history from "shared/utils/history"
//import { resetState } from "store/actions/shared"

const Auth = (props: {
    dispatch: any
    user: {
        user_id: string
        emailVerified: boolean
    }
    mode: any
    match: any
}) => {
    const { user, match, mode } = props

    useEffect(() => {
        const firstParam = match.params.first
        if (firstParam !== "bypass" && !user.user_id) {
            history.push("/")
        }
    }, [match, user.user_id])

    if (user.user_id && user.user_id !== "") {
        if (user.emailVerified) {
            return <Redirect to="/dashboard" />
        } else {
            return <Redirect to="/verify" />
        }
    } else if (mode === "Log in") {
        return (
            <div className="fractalContainer">
                <Header color="black" />
                <LoginView />
            </div>
        )
    } else if (mode === "Sign up") {
        return (
            <div className="fractalContainer">
                <Header color="black" />

                <SignupView />
            </div>
        )
    } else if (mode === "Forgot") {
        return (
            <div className="fractalContainer">
                <Header color="black" />
                <ForgotView />
            </div>
        )
    } else {
        // should never happen
        return <div />
    }
}

function mapStateToProps(state: { AuthReducer: { authFlow: any; user: any } }) {
    console.log(state)
    return {
        mode: state.AuthReducer.authFlow.mode,
        user: state.AuthReducer.user,
    }
}

export default connect(mapStateToProps)(Auth)
