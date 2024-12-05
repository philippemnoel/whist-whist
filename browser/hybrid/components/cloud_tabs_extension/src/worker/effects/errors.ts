import { mandelboxError } from "@app/worker/events/mandelbox"
import { authNetworkError } from "@app/worker/events/auth"

import { whistState } from "@app/worker/utils/state"
import { stripCloudUrl, updateTabUrl } from "@app/worker/utils/tabs"
import {
  mandelboxCreateErrorCommitHash,
  mandelboxCreateErrorUnauthorized,
  mandelboxRequest,
} from "@app/worker/utils/mandelbox"

import { AsyncReturnType } from "@app/@types/api"
import { CloudTabError } from "@app/constants/errors"

const showPopup = (type: CloudTabError) => {
  whistState.waitingCloudTabs.forEach((tab) => {
    void updateTabUrl(tab.id, stripCloudUrl(tab.url ?? ""), () => {
      setTimeout(() => {
        void chrome.tabs.sendMessage(tab.id ?? 0, { type })
      }, 1000)
    })
  })
  whistState.waitingCloudTabs = []
}

authNetworkError.subscribe(() => {
  showPopup(CloudTabError.NETWORK_ERROR)
})

mandelboxError.subscribe(
  (response: AsyncReturnType<typeof mandelboxRequest>) => {
    if (mandelboxCreateErrorCommitHash(response)) {
      showPopup(CloudTabError.UPDATE_NEEDED)
    } else if (mandelboxCreateErrorUnauthorized(response)) {
      showPopup(CloudTabError.AUTH_ERROR)
    } else {
      showPopup(CloudTabError.NO_INSTANCE_ERROR)
    }
    // TODO: need something to distinguish host errors:
    // showPopup(CloudTabError.HOST_ERROR)
  }
)
