export const UPDATE_USER = "UPDATE_USER"
export const UPDATE_AUTH_FLOW = "UPDATE_AUTH_FLOW"

export function updateUser(body: {
    user_id?: string
    name?: string
    accessToken?: string
    refreshToken?: string
    emailVerificationToken?: string
}) {
    console.log("update user action")
    return {
        type: UPDATE_USER,
        body,
    }
}

export function updateAuthFlow(body: {
    loginWarning?: string
    signupWarning?: string
    verificationEmailsSent?: number
}) {
    return {
        type: UPDATE_AUTH_FLOW,
        body,
    }
}
