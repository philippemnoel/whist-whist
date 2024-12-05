import { ReplaySubject } from "rxjs"
import { share, filter } from "rxjs/operators"

import { MandelboxState } from "@app/constants/storage"
import onChange from "on-change"
import { MandelboxInfo, HostInfo } from "@app/@types/payload"

interface WhistState {
  isLoggedIn: boolean
  waitingCloudTabs: chrome.tabs.Tab[]
  activeCloudTabs: chrome.tabs.Tab[]
  mandelboxState: MandelboxState
  mandelboxInfo: (HostInfo & MandelboxInfo) | undefined
  alreadyAddedCookies: chrome.cookies.Cookie[]
  sentInitSettings: boolean
}

interface WhistStateChange {
  path: string
  value: WhistState
  previousValue: WhistState
  applyData: {
    name: string
    args: any[]
  }
}

const _whistState: WhistState = {
  isLoggedIn: false,
  waitingCloudTabs: [],
  activeCloudTabs: [],
  mandelboxState: MandelboxState.MANDELBOX_NONEXISTENT,
  mandelboxInfo: undefined,
  alreadyAddedCookies: [],
  sentInitSettings: false,
}

const _stateDidChange = new ReplaySubject<WhistStateChange>()

const whistState = onChange(
  _whistState,
  (path, value, previousValue, applyData) => {
    _stateDidChange.next({
      path,
      value,
      previousValue,
      applyData,
    } as WhistStateChange)
  }
)

const stateDidChange = (state: string) =>
  _stateDidChange.pipe(
    filter(({ path }) => path === state),
    share()
  )

export { whistState, stateDidChange }
