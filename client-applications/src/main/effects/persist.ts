import { merge } from "rxjs"
import toPairs from "lodash.topairs"

import { fromTrigger } from "@app/utils/flows"
import { persist } from "@app/utils/persist"

// On a successful auth, store the auth credentials in Electron store
// so the user is remembered
merge(
  fromTrigger("authFlowSuccess"),
  fromTrigger("authRefreshSuccess"),
  fromTrigger("configFlowSuccess")
).subscribe(
  (args: {
    userEmail?: string
    accessToken?: string
    refreshToken?: string
    configToken?: string
  }) => {
    toPairs(args).forEach(([key, value]) => {
      if (value !== undefined) persist(key, value)
    })
  }
)
