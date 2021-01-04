import React, { useState, useEffect } from "react"
import { connect } from "react-redux"
import { PuffAnimation } from "shared/components/loadingAnimations"

import "styles/auth.css"

import Input from "shared/components/input"
import * as AuthSideEffect from "store/actions/auth/sideEffects"
import * as AuthPureAction from "store/actions/auth/pure"
import {
    checkPasswordVerbose,
    checkEmailVerbose,
    signupEnabled,
    checkEmail,
} from "pages/auth/constants/authHelpers"
import SwitchMode from "pages/auth/components/switchMode"
import PasswordConfirmForm from "shared/components/passwordConfirmForm"
// import GoogleButton from "pages/auth/components/googleButton"

const SignupView = (props: { dispatch: any; user: any; authFlow: any }) => {
    const { authFlow, user, dispatch } = props

    const [email, setEmail] = useState("")
    const [name, setName] = useState("")
    const [password, setPassword] = useState("")
    const [confirmPassword, setConfirmPassword] = useState("")
    const [emailWarning, setEmailWarning] = useState("")
    const [nameWarning, setNameWarning] = useState("")
    const [passwordWarning, setPasswordWarning] = useState("")
    const [confirmPasswordWarning, setConfirmPasswordWarning] = useState("")
    const [enteredName, setEnteredName] = useState(false)

    const [processing, setProcessing] = useState(false)

    // Dispatches signup API call
    const signup = () => {
        if (signupEnabled(email, name, password, confirmPassword)) {
            setProcessing(true)
            dispatch(AuthSideEffect.emailSignup(email, name, password))
        }
    }

    // so we can display puff while server does it's thing for google as well
    // const google_signup = (code: any) => {
    //     setProcessing(true)
    //     dispatch(AuthSideEffect.googleLogin(code))
    // }

    // Handles ENTER key press
    const onKeyPress = (evt: any) => {
        if (
            evt.key === "Enter" &&
            email.length > 4 &&
            name.length > 0 &&
            password.length > 6 &&
            email.includes("@")
        ) {
            setProcessing(true)
            dispatch(AuthSideEffect.emailSignup(email, name, password))
        }
    }

    // Updates email, password, confirm password fields as user types
    const changeEmail = (evt: any): any => {
        evt.persist()
        setEmail(evt.target.value)
    }

    const changeName = (evt: any): any => {
        if (!enteredName) {
            setEnteredName(true)
        }
        evt.persist()
        setName(evt.target.value)
    }

    const changePassword = (evt: any): any => {
        evt.persist()
        setPassword(evt.target.value)
    }

    const changeConfirmPassword = (evt: any): any => {
        evt.persist()
        setConfirmPassword(evt.target.value)
    }

    const checkNameVerbose = (name: string): any => {
        if (name.length > 0) {
            return ""
        } else {
            return "Required"
        }
    }

    const checkName = (name: string): boolean => {
        return name.length > 0
    }

    // Removes loading screen on prop change and page load
    useEffect(() => {
        setProcessing(false)
    }, [user.userID, authFlow])

    // Email and password dynamic warnings
    useEffect(() => {
        setEmailWarning(checkEmailVerbose(email))
    }, [email])

    useEffect(() => {
        setPasswordWarning(checkPasswordVerbose(password))
    }, [password])

    useEffect(() => {
        if (enteredName) {
            setNameWarning(checkNameVerbose(name))
        } else {
            setNameWarning("")
        }
    }, [name, enteredName])

    useEffect(() => {
        if (
            confirmPassword !== password &&
            password.length > 0 &&
            confirmPassword.length > 0
        ) {
            setConfirmPasswordWarning("Doesn't match")
        } else {
            setConfirmPasswordWarning("")
        }
        // we only want to change on a specific state change
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [confirmPassword])

    if (processing) {
        // Conditionally render the loading screen as we wait for signup API call to return
        return (
            <div>
                <PuffAnimation />
            </div>
        )
    } else {
        // Render the signup screen
        return (
            <div>
                <div className="auth-container">
                    <div className="auth-title">
Let's get started.
</div>
                    {authFlow &&
                        authFlow.signupWarning &&
                        authFlow.signupWarning !== "" && (
                            <div
                                style={{
                                    width: "100%",
                                    background: "#ff5627",
                                    padding: "15px 20px",
                                    color: "white",
                                    fontSize: 14,
                                    marginTop: 5,
                                    marginBottom: 20,
                                }}
                            >
                                {authFlow.signupWarning}
                            </div>
                        )}
                    <div style={{ marginTop: 13 }}>
                        <Input
                            text="Email"
                            type="email"
                            placeholder="bob@gmail.com"
                            onChange={changeEmail}
                            onKeyPress={onKeyPress}
                            value={email}
                            warning={emailWarning}
                            valid={checkEmail(email)}
                        />
                    </div>
                    <div style={{ marginTop: 13 }}>
                        <Input
                            text="Name"
                            type="name"
                            placeholder="Bob"
                            onChange={changeName}
                            onKeyPress={onKeyPress}
                            value={name}
                            warning={nameWarning}
                            valid={checkName(name)}
                        />
                    </div>
                    <PasswordConfirmForm
                        changePassword={changePassword}
                        changeConfirmPassword={changeConfirmPassword}
                        onKeyPress={onKeyPress}
                        password={password}
                        confirmPassword={confirmPassword}
                        passwordWarning={passwordWarning}
                        confirmPasswordWarning={confirmPasswordWarning}
                        isFirstElement={false}
                    />
                    <button
                        className="purple-button"
                        style={{
                            opacity: signupEnabled(
                                email,
                                name,
                                password,
                                confirmPassword
                            )
                                ? 1.0
                                : 0.6,
                        }}
                        onClick={signup}
                        disabled={
                            !signupEnabled(
                                email,
                                name,
                                password,
                                confirmPassword
                            )
                        }
                    >
                        Sign up
                    </button>
                    <div className="line" />
                    {/* <GoogleButton login={google_signup} /> */}
                    <div style={{ marginTop: 20 }}>
                        <SwitchMode
                            question="Already have an account?"
                            link="Log in here"
                            closer="."
                            onClick={() =>
                                dispatch(
                                    AuthPureAction.updateAuthFlow({
                                        mode: "Log in",
                                    })
                                )
                            }
                        />
                    </div>
                </div>
            </div>
        )
    }
}

function mapStateToProps(state: { AuthReducer: { user: any; authFlow: any } }) {
    return {
        user: state.AuthReducer.user,
        authFlow: state.AuthReducer.authFlow,
    }
}

export default connect(mapStateToProps)(SignupView)
