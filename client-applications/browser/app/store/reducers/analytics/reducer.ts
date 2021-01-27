import { DEFAULT, Timer } from "store/reducers/analytics/default"
import * as AnalyticsAction from "store/actions/analytics/pure"
import { deepCopyObject } from "shared/utils/general/reducer"

export default function (
    state = DEFAULT,
    action: {
        body: Timer
        type: string
    }
) {
    const stateCopy = deepCopyObject(state)

    switch (action.type) {
        case AnalyticsAction.UPDATE_TIMER:
            return {
                ...stateCopy,
                timer: Object.assign(stateCopy.timer, action.body),
            }
        default:
            return state
    }
}
