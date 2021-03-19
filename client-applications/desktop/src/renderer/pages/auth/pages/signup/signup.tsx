import React, { useState, useEffect } from "react"
import { Flipped, Flipper } from "react-flip-toolkit"
import FadeIn from "react-fade-in"

import { Logo } from "@app/renderer/pages/auth/shared/components/logo"
import { FractalInput, FractalInputState } from "@app/components/html/input"
import { AuthWarning, InputWarning } from "@app/components/custom/warning"
import { FractalButton, FractalButtonState } from "@app/components/html/button"
import {
    checkEmailVerbose,
    signupEnabled,
    checkEmail,
    checkPassword,
} from "@app/renderer/pages/auth/shared/helpers/authHelpers"
import { emailSignup } from "@app/renderer/pages/auth/pages/signup/shared/utils/api"

import { fractalSignupWarning } from "@app/renderer/pages/auth/pages/signup/shared/utils/constants"

const Signup = (props: { onSignup: (json: object) => void }) => {
    /*
        Component for signing up for Fractal
        Contains the form to signup, and also dispatches an API request to
        the server to authenticate the user.
        Arguments:
            dispatch (Dispatch<any>): Action dispatcher
            user (User): User from Redux state
            authFlow (AuthFlow): AuthFlow from Redux state
    */
    const { onSignup } = props

    const [email, setEmail] = useState("")
    const [name, setName] = useState("")
    const [password, setPassword] = useState("")
    const [confirmPassword, setConfirmPassword] = useState("")

    const [processing, setProcessing] = useState(false)
    const [signupWarning, setSignupWarning] = useState("")

    // Dispatches signup API call
    const signup = () => {
        if (signupEnabled(email, name, password, confirmPassword)) {
            setProcessing(true)
            emailSignup(email, password, name, "").then(
                ({ json, response }) => {
                    if (json && response.status === 200) {
                        setSignupWarning(fractalSignupWarning.NONE)
                        onSignup(json)
                    } else {
                        setSignupWarning(fractalSignupWarning.ACCOUNT_EXISTS)
                        setEmail("")
                        setPassword("")
                        setName("")
                        setConfirmPassword("")
                    }
                    setProcessing(false)
                }
            )
        } else {
            setProcessing(false)
        }
    }

    const checkName = (n: string): boolean => {
        return n.length > 0
    }

    const buttonState = () => {
        if (processing) {
            return FractalButtonState.PROCESSING
        } else {
            if (signupEnabled(email, name, password, confirmPassword)) {
                return FractalButtonState.DEFAULT
            } else {
                return FractalButtonState.DISABLED
            }
        }
    }

    return (
        <div className="flex flex-col justify-center items-center bg-white h-screen text-center">
            <div className="w-full max-w-xs m-auto">
                <FadeIn>
                    <Logo />
                    <h5 className="font-body mt-8 text-xl mb-6 font-semibold">
                        Sign up to get started
                    </h5>
                    <InputWarning warning={checkEmailVerbose(email)} />
                    <AuthWarning warning={signupWarning} />
                    <Flipper
                        flipKey={checkPassword(password).toString()}
                        spring="stiff"
                    >
                        <Flipped flipId="form">
                            <ul className="listContents">
                                <div key="email">
                                    <h5 className="font-body text-left font-semibold mt-4 text-sm">
                                        Email
                                    </h5>
                                    <FractalInput
                                        type="email"
                                        placeholder="Email"
                                        onChange={(email: string) =>
                                            setEmail(email)
                                        }
                                        onEnterKey={signup}
                                        value={email}
                                        state={
                                            checkEmail(email)
                                                ? FractalInputState.SUCCESS
                                                : FractalInputState.DEFAULT
                                        }
                                        className="mt-1"
                                    />
                                </div>
                                <div key="password">
                                    <h5 className="font-body text-left font-semibold mt-4 text-sm">
                                        Password
                                    </h5>
                                    <FractalInput
                                        type="password"
                                        placeholder="Password"
                                        onChange={(password: string) =>
                                            setPassword(password)
                                        }
                                        onEnterKey={signup}
                                        value={password}
                                        state={
                                            checkPassword(password)
                                                ? FractalInputState.SUCCESS
                                                : FractalInputState.DEFAULT
                                        }
                                        className="mt-1"
                                    />
                                </div>
                                <div key="confirm-password">
                                    {checkPassword(password) && (
                                        <>
                                            <h5 className="font-body text-left font-semibold mt-4 text-sm">
                                                Confirm Password
                                            </h5>
                                            <FractalInput
                                                type="password"
                                                placeholder="Password"
                                                onChange={(password: string) =>
                                                    setConfirmPassword(password)
                                                }
                                                onEnterKey={signup}
                                                value={confirmPassword}
                                                state={
                                                    checkPassword(password)
                                                        ? FractalInputState.SUCCESS
                                                        : FractalInputState.DEFAULT
                                                }
                                                className="mt-1"
                                            />
                                        </>
                                    )}
                                </div>
                            </ul>
                        </Flipped>
                    </Flipper>
                    <FractalButton
                        contents="Sign up"
                        className="mt-4 w-full"
                        state={buttonState()}
                        onClick={signup}
                    />
                    <FractalNavigation
                        url="/auth/login"
                        text="Already have an account? Log in here."
                        linkText="here"
                        className="relative top-4"
                    />
                </FadeIn>
            </div>
        </div>
    )
}

export default Signup
