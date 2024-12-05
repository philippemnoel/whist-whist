import { merge, share } from "rxjs"

import { serverCookiesSynced } from "@app/worker/events/cookies"
import { languagesInitialized } from "@app/worker/events/language"

// Include all events that reflect tasks that need to have been completed 
//     before we begin creating cloud tabs
const initSettingsSent = merge(
  serverCookiesSynced,
  languagesInitialized,
).pipe(share())

export { initSettingsSent }
