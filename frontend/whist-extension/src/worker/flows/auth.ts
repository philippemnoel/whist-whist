import has from "lodash.has"
import isEmpty from "lodash.isempty"

import { setStorage } from "@app/worker/utils/storage"
import {
  isTokenExpired,
  authInfoParse,
  authInfoRefreshRequest,
  getCachedAuthInfo,
  authPortalURL,
  authInfoCallbackRequest,
} from "@app/worker/utils/auth"
import { createAuthTab } from "@app/worker/utils/tabs"

import { CACHED_AUTH_INFO } from "@app/constants/storage"

const refreshFlow = async (authInfo: any) => {
  const response = await authInfoRefreshRequest(authInfo?.refreshToken)
  const json = await response.json()
  return authInfoParse(json)
}

const authenticateCachedAuthInfo = async () => {
  let authInfo = await getCachedAuthInfo()

  if (authInfo?.refreshToken === undefined) return false

  if (await isTokenExpired(authInfo?.accessToken ?? "")) {
    authInfo = await refreshFlow(authInfo)

    if (has(authInfo, "error")) return false

    await setStorage(
      CACHED_AUTH_INFO,
      JSON.stringify({
        userEmail: authInfo.userEmail,
        accessToken: authInfo.accessToken,
        refreshToken: authInfo?.refreshToken,
      })
    )
  }

  return true
}

const googleAuth = () => {
  chrome.identity.launchWebAuthFlow(
    {
      url: authPortalURL(),
      interactive: true,
    },
    async (callbackUrl) => {
      const response = (await authInfoCallbackRequest(callbackUrl)) as any
      const json = await response?.json()
      const authInfo = authInfoParse(json)

      await setStorage(
        CACHED_AUTH_INFO,
        JSON.stringify({
          userEmail: authInfo.userEmail,
          accessToken: authInfo.accessToken,
          refreshToken: response?.json?.refresh_token,
        })
      )
    }
  )
}

const authenticate = async () => {
  const wasLoggedIn = !isEmpty(await getCachedAuthInfo())

  if (!wasLoggedIn) {
    createAuthTab()
    return false
  } else {
    const authenticated = authenticateCachedAuthInfo()

    if (!authenticated) {
      createAuthTab()
      return false
    }

    return true
  }
}

export { googleAuth, authenticate }
