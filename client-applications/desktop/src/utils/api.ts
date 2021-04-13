import { configGet, configPost } from '@fractal/core-ts'
import { createConfigToken, decryptConfigToken } from '@app/utils/crypto'
import config from '@app/utils/config'
import { AsyncReturnType } from '@app/utils/types'

/* eslint-disable @typescript-eslint/naming-convention */

/*
 * @fractal/core-ts http functions like "get" and "post"
 * are constructed at runtime so they can be passed a config
 * object, like the httpConfig object below.
 *
 * Naming across config objects will be consistent as core-ts grows.
 * Common prefixes will include:
 *
 * handle - A function that will accept a request or response object, to
 * be called for its side effects upon certain events, like error.
 *
 * endpoint - A string with the format "/api/endpoint", will be
 * concatenated onto a server prefix to form a complete URL.
 *
 */

const httpConfig = {
  server: config.url.WEBSERVER_URL,
  // handleAuth: (_: any) => goTo("/auth"),
  endpointRefreshToken: '/token/refresh'
}

export const get = configGet(httpConfig)

export const post = configPost(httpConfig)

export const emailLogin = async (username: string, password: string) =>
  post({
    endpoint: '/account/login',
    body: { username, password }
  })

type ResponseAuth = AsyncReturnType<typeof emailLogin>

export const emailLoginValid = (response: ResponseAuth) =>
  (response?.json?.access_token ?? '') !== ''

export const emailLoginError = (response: ResponseAuth) =>
  response.status !== 200

export const emailLoginAccessToken = (response: ResponseAuth) =>
  response.json?.access_token

export const emailLoginRefreshToken = (response: ResponseAuth) =>
  response.json?.refresh_token

export const emailLoginConfigToken = async (
  response: ResponseAuth,
  password: string
) =>
  (response?.json?.encrypted_config_token ?? '') !== ''
    ? decryptConfigToken(response.json.encrypted_config_token, password)
    : await createConfigToken()

export const emailSignup = async (
  username: string,
  password: string,
  encrypted_config_token: string
) =>
  post({
    endpoint: '/account/register',
    body: {
      username,
      password,
      encrypted_config_token,
      name: '',
      feedback: ''
    }
  })

export const emailSignupValid = emailLoginValid

export const emailSignupError = (response: ResponseAuth) => {
  // A 400 bad response indicates that the user account exists,
  // we consider this a warning, not a failure.
  if (response.json.status === 400) return false
  if (response.json.status === 200) return false
  return true
}

export const emailSignupAccessToken = emailLoginAccessToken

export const emailSignupRefreshToken = emailLoginRefreshToken

export const tokenValidate = async (accessToken: string) =>
  get({ endpoint: '/token/validate', accessToken })

export const taskStatus = async (taskID: string, accessToken: string) =>
  get({ endpoint: '/status/' + taskID, accessToken })

export const containerRequest = async (
  username: string,
  accessToken: string,
  region: string,
  dpi: number
) =>
  post({
    endpoint: '/container/assign',
    accessToken,
    body: {
      username,
      region,
      dpi,
      app: 'Google Chrome'
    }
  })

export const regionRequest = async (username: string, accessToken: string) =>
  get({
    endpoint: `/regions?username=${username}`,
    accessToken
  })
