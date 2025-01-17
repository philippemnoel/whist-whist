import { Storage } from "@app/constants/storage"

import { webRequest, webNavigationError } from "@app/worker/events/webRequest"
import { getStorage } from "@app/worker/utils/storage"
import {
  getTab,
  updateTabUrl,
  unmarkActiveCloudTab,
  stripCloudUrl,
} from "@app/worker/utils/tabs"

/* eslint-disable @typescript-eslint/strict-boolean-expressions */
webRequest.subscribe((response: any) => {
  void Promise.all([
    getStorage<string[]>(Storage.SAVED_CLOUD_URLS),
    getTab(response.tabId),
  ]).then((args: any) => {
    const [savedCloudUrls, tab] = args
    const url = new URL(response.url)
    const host = url.hostname
    const currentHost = new URL(stripCloudUrl(tab.url)).hostname
    const isSaved = (savedCloudUrls ?? []).includes(host)
    const isCloudUrl = response.url.startsWith("cloud:")

    if (!isSaved && !isCloudUrl) return

    if (!isCloudUrl && host !== currentHost) {
      unmarkActiveCloudTab(tab)
      void updateTabUrl(tab.id, `cloud:${response.url as string}`)
    }
  })
})

webNavigationError.subscribe((response: any) => {
  // net::ERR_BLOCKED_BY_ADMINISTRATOR means that this navigation was blocked by a URL blocklist.
  // All blocklisted URLs in the top level (frame ID 0) should be converted to cloud URLs.
  if (
    response.frameId === 0 &&
    response.error === "net::ERR_BLOCKED_BY_ADMINISTRATOR"
  ) {
    void Promise.all([getTab(response.tabId)]).then((args: any) => {
      const [tab] = args

      const sleepTimeMs = 80
      const url = response.url

      // The goBack() erases the history of original blocklisted URL, which will cause weird
      // behavior when the users reach it with the "navigate back" button.
      // However due to limitations of the extension API, a short sleep has to be added between
      // goBack() and updateTabUrl().
      // TODO: replace this with a better solution, as sleep is not ideal
      chrome.tabs.goBack(tab.id, () => {
        void new Promise((resolve) => setTimeout(resolve, sleepTimeMs)).then(
          () => {
            void updateTabUrl(tab.id, `cloud:${url as string}`)
          }
        )
      })
    })
  }
})
