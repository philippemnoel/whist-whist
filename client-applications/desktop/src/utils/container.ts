import { screen } from "electron"
import { pick } from "lodash"

import { get, post } from "@app/utils/api"
import { defaultAllowedRegions, AWSRegion } from "@app/@types/aws"
import { chooseRegion } from "@app/utils/region"
import { AsyncReturnType } from "@app/@types/state"

// For the purposes of the low-level rendering which is performed by the
// protocol, the default DPI is always 96; this is modified by a scale factor
// on high-resolution monitors, as they scale up assets to keep sizes
// consistent with their low-resolution counterparts.
const getDPI = () => screen.getPrimaryDisplay().scaleFactor * 96

export const regionGet = async (sub: string, accessToken: string) => {
  const regions: Record<string, any> = await regionRequest(sub, accessToken)
  const allowedRegions = (regions?.json as AWSRegion[]) ?? []

  if (allowedRegions.length === 0) {
    return await chooseRegion(defaultAllowedRegions)
  } else {
    return await chooseRegion(regions.json)
  }
}

export const containerCreate = async (sub: string, accessToken: string) => {
  const region = await regionGet(sub, accessToken)
  const response = await containerRequest(sub, accessToken, region, getDPI())
  return response
}

export const containerCreateSuccess = (
  response: AsyncReturnType<typeof containerCreate>
) => [200, 202].includes(response.status as number)

export const containerCreateErrorNoAccess = (
  response: AsyncReturnType<typeof containerCreate>
) => response.status === 402

export const containerCreateErrorUnauthorized = (
  response: AsyncReturnType<typeof containerCreate>
) => response.status === 422 || response.status === 401

export const containerCreateErrorInternal = (
  response: AsyncReturnType<typeof containerCreate>
) =>
  (response?.json?.ID ?? "") === "" &&
  !containerCreateErrorNoAccess(response) &&
  !containerCreateErrorUnauthorized(response)

export const containerPolling = async (taskID: string, accessToken: string) =>
  await taskStatus(taskID, accessToken)

export const containerPollingError = (
  response: AsyncReturnType<typeof containerPolling>
) => (response?.json?.state ?? "") === ""

export const containerPollingSuccess = (
  response: AsyncReturnType<typeof containerPolling>
) => response?.json?.state === "SUCCESS"

export const containerPollingPending = (
  response: AsyncReturnType<typeof containerPolling>
) => response?.json?.state !== "SUCCESS" && response?.json?.state !== "FAILURE"

export const containerPollingPorts = (
  response: AsyncReturnType<typeof containerPolling>
) => pick(response?.json?.output, ["port_32262", "port_32263", "port_32273"])

export const containerPollingIP = (
  response: AsyncReturnType<typeof containerPolling>
) => response?.json?.output?.ip

export const containerPollingSecretKey = (
  response: AsyncReturnType<typeof containerPolling>
) => response?.json?.output?.secret_key

// Helper functions

const taskStatus = async (taskID: string, accessToken: string) =>
  get({ endpoint: "/status/" + taskID, accessToken })

const containerRequest = async (
  username: string,
  accessToken: string,
  region: string,
  dpi: number
) =>
  post({
    endpoint: "/container/assign",
    accessToken,
    body: {
      username,
      region,
      dpi,
      app: "Google Chrome",
    },
  })

const regionRequest = async (username: string, accessToken: string) =>
  get({
    endpoint: `/regions?username=${username}`,
    accessToken,
  })
