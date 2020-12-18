export const REMEMBER_ME_LOGIN = "REMEMBER_ME_LOGIN"
export const VALIDATE_ACCESS_TOKEN = "VALIDATE_ACCESS_TOKEN"

export const CREATE_CONTAINER = "CREATE_CONTAINER"

export const SUBMIT_FEEDBACK = "SUBMIT_FEEDBACK"

export const CANCEL_CONTAINER = "CANCEL_CONTAINER"

export const GET_STATUS = "GET_STATUS"
export const DISCONNECT_APP = "DISCONNECT_APP"

export function rememberMeLogin(username: string) {
    return {
        type: REMEMBER_ME_LOGIN,
        username,
    }
}

export function createContainer(app: string, url: null | string, test = false) {
    return {
        type: CREATE_CONTAINER,
        app,
        url,
        test,
    }
}

export function cancelContainer(test = false) {
    return {
        type: CANCEL_CONTAINER,
        test,
    }
}

export function validateAccessToken(accessToken: string) {
    return {
        type: VALIDATE_ACCESS_TOKEN,
        accessToken,
    }
}

export function submitFeedback(feedback: string, feedbackType: string) {
    return {
        type: SUBMIT_FEEDBACK,
        feedback,
        feedbackType,
    }
}

export function getStatus(id: string) {
    return {
        type: GET_STATUS,
        id,
    }
}
export function disconnectApp(app: string) {
    return {
        type: DISCONNECT_APP,
        app,
    }
}
