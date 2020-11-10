import React, { useEffect, useState } from "react"
import { connect } from "react-redux"

import { PuffAnimation } from "shared/components/loadingAnimations"
import {
    validateVerificationToken,
    sendVerificationEmail,
} from "store/actions/auth/sideEffects"
import { updateAuthFlow, resetUser } from "store/actions/auth/pure"
import history from "shared/utils/history"
import { DivSpace, Title } from "pages/auth/components/authUtils"

import "styles/auth.css"

const RetryButton = (props: {
    text: string
    checkEmail: boolean
    onClick: (evt: any) => any
}) => (
    <button
        className="white-button"
        style={{
            width: "100%",
            marginTop: 15,
            background: "#3930b8",
            border: "none",
            color: "white",
            fontSize: 16,
            paddingTop: 15,
            paddingBottom: 15,
            opacity: props.checkEmail ? 1.0 : 0.6,
        }}
        onClick={props.onClick}
        disabled={!props.checkEmail}
    >
        {props.text}
    </button>
)

const VerifyView = (props: {
    dispatch: any
    user: any
    authFlow: any
    token: any
    validToken: boolean
}) => {
    const { dispatch, user, authFlow, token, validToken } = props

    // visual state constants
    const [processing, setProcessing] = useState(false)
    const [canRetry, setCanRetry] = useState(true)
    const [sentRetry, setSentRetry] = useState(false)

    // logic
    const validUser =
        user.user_id &&
        user.user_id !== "" &&
        user.accessToken &&
        user.accessToken !== ""

    // used for button
    const retryMessage = validUser
        ? sentRetry
            ? "Sent!"
            : canRetry
            ? "Send Again"
            : "Sending..."
        : "Login to Re-send"

    const reset = () => {
        dispatch(resetUser())
        history.push("/auth/bypass")
    }

    const sendWithDelay = (evt: any) => {
        // this can only be reached when there is a valid user
        setCanRetry(false)

        dispatch(
            updateAuthFlow({
                verificationEmailsSent: authFlow.verificationEmailsSent
                    ? authFlow.verificationEmailsSent + 1
                    : 1,
            })
        )
        dispatch(
            sendVerificationEmail(user.user_id, user.emailVerificationToken)
        )
        setTimeout(() => {
            // first show them that it's been sent
            setSentRetry(true)
            setTimeout(() => {
                // then return to the original state
                setCanRetry(true)
                setSentRetry(false)
            }, 3000)
        }, 2000)
    }

    useEffect(() => {
        if (validUser && validToken && !processing) {
            dispatch(validateVerificationToken(token))
        }
        setProcessing(true)
        // want onComponentMount basically (thus [] ~ no deps ~ called only at the very beginning)
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [])

    useEffect(() => {
        if (
            authFlow.verificationStatus === "success" ||
            authFlow.verificationStatus === "failed"
        ) {
            setProcessing(false)
        }
    }, [authFlow.verificationStatus])

    if (!validToken) {
        return (
            <div
                style={{
                    width: 400,
                    margin: "auto",
                    marginTop: 70,
                }}
            >
                <Title
                    title="Check your inbox to verify your email"
                    subtitle="(and/or spam)"
                />
                <DivSpace height={40} />
                <RetryButton
                    text={retryMessage}
                    checkEmail={validUser && canRetry}
                    onClick={sendWithDelay}
                />
                <button
                    className="white-button"
                    style={{ width: "100%", marginTop: 20, fontSize: 16 }}
                    onClick={reset}
                >
                    Back to Login
                </button>
            </div>
        )
    } else {
        if (processing) {
            // Conditionally render the loading screen as we wait for signup API call to return
            return (
                <div>
                    <PuffAnimation />
                </div>
            )
        } else {
            // this state is reached after processing has finished and failed
            return (
                <div
                    style={{
                        width: 400,
                        margin: "auto",
                        marginTop: 70,
                    }}
                >
                    {authFlow.verificationStatus === "success" && (
                        <>
                            <Title
                                title="Successfully verified email!"
                                subtitle="Redirecting you back to the homepage"
                            />
                            <PuffAnimation />
                        </>
                    )}
                    {authFlow.verificationStatus === "failed" && (
                        <>
                            <Title title="Failed to verify" />
                            <DivSpace height={40} />
                            <RetryButton
                                text={retryMessage}
                                checkEmail={validUser && canRetry}
                                onClick={sendWithDelay}
                            />
                        </>
                    )}
                </div>
            )
        }
    }
}

function mapStateToProps(state: { AuthReducer: { user: any; authFlow: any } }) {
    return {
        user: state.AuthReducer.user ? state.AuthReducer.user : {},
        authFlow: state.AuthReducer.authFlow ? state.AuthReducer.authFlow : {},
    }
}

export default connect(mapStateToProps)(VerifyView)
