/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file api.ts
 * @brief This file contains utility functions for HTTP requests.
 */

import stringify from "json-stringify-safe"

const httpConfig =
  (method: string) =>
  async (args: { body: object; url: string; accessToken?: string }) => {
    const response = await fetch(args.url, {
      method,
      headers: {
        "Content-Type": "application/json",
        ...(args.accessToken !== undefined && {
          Authorization: `Bearer ${args.accessToken}`,
        }),
      },
      body: stringify(args.body),
    })

    try {
      const json = await response.json()

      return {
        status: response.status,
        json,
      }
    } catch (err) {
      return {
        status: response.status,
        json: {},
      }
    }
  }

const post = httpConfig("POST")
const put = httpConfig("PUT")

export { post, put }