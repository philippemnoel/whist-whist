import { from, of, merge } from "rxjs"
import { switchMap, map, filter, share } from "rxjs/operators"

import { getStorage } from "@app/worker/utils/storage"
import {
  mandelboxRequest,
  mandelboxCreateSuccess,
} from "@app/worker/utils/mandelbox"
import { stateDidChange, whistState } from "@app/worker/utils/state"
import { authSuccess } from "./auth"

import { MandelboxState, Storage } from "@app/constants/storage"
import { config } from "@app/constants/app"
import { AsyncReturnType } from "@app/@types/api"
import { AWSRegion, AWSRegionOrdering } from "@app/constants/location"
import { AuthInfo } from "@app/@types/payload"

const mandelboxNeeded = merge(
  stateDidChange("waitingCloudTabs").pipe(
    filter((change: any) => change?.applyData?.name === "push")
  ),
  authSuccess.pipe(filter(() => whistState.waitingCloudTabs.length > 0))
).pipe(
  filter(
    () =>
      whistState.mandelboxState === MandelboxState.MANDELBOX_NONEXISTENT &&
      whistState.isLoggedIn
  ),
  share()
)

const mandelboxInfo = mandelboxNeeded.pipe(
  switchMap(() =>
    from(
      Promise.all([
        getStorage<AuthInfo>(Storage.AUTH_INFO),
        getStorage<AWSRegionOrdering>(Storage.CLOSEST_AWS_REGIONS),
      ])
    )
  ),
  switchMap(([auth, regions]) =>
    config.HOST_IP === undefined
      ? from(
          mandelboxRequest(
            auth.accessToken ?? "",
            regions.map((r: { region: AWSRegion }) => r.region),
            auth.userEmail ?? "",
            "1.0.0"
          )
        )
      : of({
          status: 200,
          json: {
            ip: config.HOST_IP,
            mandelbox_id: (crypto as any).randomUUID(),
          },
        })
  ),
  share()
)

const mandelboxSuccess = mandelboxInfo.pipe(
  filter(
    (response: AsyncReturnType<typeof mandelboxRequest>) =>
      mandelboxCreateSuccess(response) || config.HOST_IP !== undefined
  ),
  map((response: AsyncReturnType<typeof mandelboxRequest>) => ({
    mandelboxIP: response.json.ip,
    mandelboxID: response.json.mandelbox_id,
  })),
  share()
)

const mandelboxError = mandelboxInfo.pipe(
  filter(
    (response: AsyncReturnType<typeof mandelboxRequest>) =>
      !mandelboxCreateSuccess(response)
  )
)

// TODO: Implement mandelbox failure events
export { mandelboxNeeded, mandelboxSuccess, mandelboxError }
