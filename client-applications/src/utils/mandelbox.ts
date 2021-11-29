/**
 * Copyright 2021 Whist Technologies, Inc.
 * @file mandelbox.ts
 * @brief This file contains utility functions interacting with the webserver to create mandelboxes.
 */

import isEmpty from "lodash.isempty"

import { post } from "@app/utils/api"
import { AWSRegion } from "@app/@types/aws"
import { sessionID } from "@app/constants/app"
import { AsyncReturnType } from "@app/@types/state"
import { appEnvironment, FractalEnvironments } from "../../config/configs"
import config from "@app/config/environment"

const COMMIT_SHA = config.keys.COMMIT_SHA

const isLocalEnv = () => {
  const isLocal = appEnvironment === FractalEnvironments.LOCAL
  if (!isLocal && (isEmpty(COMMIT_SHA) || COMMIT_SHA === undefined)) {
    console.log("COMMIT_SHA is empty when appEnvironment is not LOCAL!")
    console.log("No COMMIT_SHA may create issues communicating with server.")
  }
  return isLocal
}

export const mandelboxCreateSuccess = (
  response: AsyncReturnType<typeof mandelboxRequest>
) => [200, 202].includes(response.status as number)

export const mandelboxCreateErrorNoAccess = (
  response: AsyncReturnType<typeof mandelboxRequest>
) => response.status === 402

export const mandelboxCreateErrorUnauthorized = (
  response: AsyncReturnType<typeof mandelboxRequest>
) => response.status === 422 || response.status === 401

export const mandelboxCreateErrorMaintenance = (
  response: AsyncReturnType<typeof mandelboxRequest>
) => response.status === 512

export const mandelboxCreateErrorInternal = (
  response: AsyncReturnType<typeof mandelboxRequest>
) =>
  (response?.json?.ID ?? "") === "" &&
  !mandelboxCreateErrorNoAccess(response) &&
  !mandelboxCreateErrorUnauthorized(response) &&
  !mandelboxCreateErrorMaintenance(response)

// Helper functions
export const mandelboxRequest = async (
  accessToken: string,
  regions: AWSRegion[],
  userEmail: string
) =>
  post({
    endpoint: "/mandelbox/assign",
    accessToken,
    body: {
      regions,
      client_commit_hash: isLocalEnv() ? "local_dev" : COMMIT_SHA,
      session_id: sessionID,
      user_email: userEmail,
    },
  })
