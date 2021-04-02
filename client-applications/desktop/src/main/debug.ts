// import { identity } from "lodash"
import { isObservable } from "rxjs"
import { identity, pick } from "lodash"
import { logDebug } from "@app/utils/logging"
import * as login from "@app/main/login"
import * as signup from "@app/main/signup"
import * as container from "@app/main/container"
import * as protocol from "@app/main/protocol"
import * as user from "@app/main/user"
import * as error from "@app/main/error"
import * as events from "@app/main/events"

const modules = [login, container, protocol, user, error, signup, events]

type DebugSchema = {
    [title: string]:
        | [string]
        | [string, null]
        | [string, (...args: any[]) => any]
}

const schema: DebugSchema = {
    eventIPC: ["value:"],
    userEmail: ["value:"],
    userPassword: ["value:", () => "********"],
    userAccessToken: ["value:"],
    userRefreshToken: ["value:"],
    userConfigToken: ["value:"],
    signupRequest: ["signup requested", null],
    signupLoading: ["value:"],
    signupWarning: ["signed up with invalid credentials", null],
    signupFailure: ["value:"],
    signupSuccess: [
        "printing only tokens:",
        ({ json }) => ({
            accessToken: json.access_token?.substring(0, 6) + "...",
            refreshToken: json.refresh_token?.substring(0, 6) + "...",
        }),
    ],
    loginRequest: ["login requested", null],
    loginLoading: ["value:"],
    loginWarning: ["logged in with invalid credentials", null],
    loginFailure: ["value:"],
    loginSuccess: [
        "printing only tokens:",
        ({ json }) => ({
            accessToken: json.access_token?.substring(0, 6) + "...",
            refreshToken: json.refresh_token?.substring(0, 6) + "...",
        }),
    ],
    containerCreateRequest: ["value:"],
    containerCreateLoading: ["value:"],
    containerCreateSuccess: ["printing only taskID:", ({ json }) => json.ID],
    containerCreateFailure: ["error:"],
    containerAssignRequest: ["value:"],
    containerAssignLoading: ["value:"],
    containerAssignPolling: ["state value"],
    containerAssignSuccess: ["value:"],
    containerAssignFailure: ["error:"],
    protocolLaunchRequest: [
        "value:",
        ({ connected, exitCode, pid }) => ({
            connected,
            exitCode,
            pid,
        }),
    ],
    protocolLaunchLoading: ["value:"],
    protocolLaunchSuccess: ["value:"],
    protocolLaunchFailure: ["error:"],
    protocolCloseRequest: [
        "printing subset of protocol object:",
        ([protocol]) =>
            pick(protocol, [
                "killed",
                "connected",
                "exitCode",
                "signalCode",
                "pid",
            ]),
    ],
    errorRelaunchRequest: ["relaunching app due to error!"],
}

const symbols = modules.reduce((acc, m) => ({ ...acc, ...m }), {}) as any

for (let key in symbols)
    if (isObservable(symbols[key as string]) && schema[key]) {
        symbols[key].subscribe((...args: any) => {
            let [message, func] = schema[key]
            if (func === undefined) func = identity
            let data = func ? func(...args) : undefined
            logDebug(key, message, data)
        })
    }
