import { DEFAULT } from "store/reducers/auth/default"

import * as PureAction from "store/actions/auth/pure"
import * as SharedAction from "store/actions/shared"

import { deep_copy } from "shared/utils/reducerHelpers"

const AuthReducer = (state = DEFAULT, action: any) => {
    const stateCopy: any = deep_copy(state)
    switch (action.type) {
        case PureAction.UPDATE_USER:
            return {
                ...stateCopy,
                user: Object.assign(stateCopy.user, action.body),
            }
        case PureAction.UPDATE_AUTH_FLOW:
            console.log("auth flow reducer")
            return {
                ...stateCopy,
                authFlow: Object.assign(stateCopy.authFlow, action.body),
            }
        case PureAction.RESET_USER:
            return {
                ...stateCopy,
                user: DEFAULT.user,
            }
        case SharedAction.RESET_STATE:
            return DEFAULT
        case SharedAction.REFRESH_STATE:
            // enforce structure of default but add in old information
            const defaultCopy: any = deep_copy(DEFAULT)

            Object.keys(stateCopy).forEach((outerKey: any) => {
                Object.keys(stateCopy[outerKey]).forEach((innerKey: string) => {
                    if (
                        stateCopy[outerKey][innerKey] &&
                        defaultCopy.hasOwnProperty(outerKey) &&
                        defaultCopy[outerKey].hasOwnProperty(innerKey)
                    ) {
                        defaultCopy[outerKey][innerKey] =
                            stateCopy[outerKey][innerKey]
                    }
                })
            })

            return defaultCopy
        default:
            return state
    }
}

export default AuthReducer
