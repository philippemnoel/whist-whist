import { PopupError } from "@app/constants/errors"
import {
  videoChatUrls,
  videoUrls,
  unsupportedUrls,
  whistUrls,
} from "@app/constants/urls"
import { isCloudTab, stripCloudUrl } from "@app/worker/utils/tabs"

const isInvalidScheme = (url: URL) =>
  url.protocol !== "http:" && url.protocol !== "https:"
const isVideoChatUrl = (url: URL) =>
  videoChatUrls.some((el) => url.hostname.includes(el))
const isVideoUrl = (url: URL) =>
  videoUrls.some((el) => url.hostname.includes(el))
const isUnsupportedUrl = (url: URL) =>
  unsupportedUrls.some((el) => url.hostname.includes(el))
const isWhistUrl = (url: URL) =>
  whistUrls.some((el) => url.hostname.includes(el))

const cannotStreamError = (tab: chrome.tabs.Tab | undefined) => {
  if (tab === undefined || tab.id === undefined || tab.url === undefined)
    return PopupError.IS_INVALID_URL

  if (isCloudTab(tab)) return undefined

  if (tab.incognito) return PopupError.IS_INCOGNITO

  if (!tab.url.startsWith("http")) return PopupError.IS_INVALID_SCHEME

  const url = new URL(stripCloudUrl(tab.url))

  if (isInvalidScheme(url)) return PopupError.IS_INVALID_SCHEME
  if (isVideoChatUrl(url)) return PopupError.IS_VIDEO_CHAT
  if (isVideoUrl(url)) return PopupError.IS_VIDEO
  if (isUnsupportedUrl(url)) return PopupError.IS_UNSUPPORTED_URL
  if (isWhistUrl(url)) return PopupError.IS_WHIST_URL

  return undefined
}

export { cannotStreamError }