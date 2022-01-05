/**
 * Copyright (c) 2020-2022 Whist Technologies, Inc.
 * @file error.ts
 * @brief This file contains various error messages.
 */

export const NO_PAYMENT_ERROR = "NO_PAYMENT_ERROR"
export const UNAUTHORIZED_ERROR = "UNAUTHORIZED_ERROR"
export const PROTOCOL_ERROR = "PROTOCOL_ERROR"
export const MANDELBOX_INTERNAL_ERROR = "MANDELBOX_INTERNAL_ERROR"
export const COMMIT_HASH_MISMATCH = "COMMIT_HASH_MISMATCH"
export const COULD_NOT_LOCK_INSTANCE = "COULD_NOT_LOCK_INSTANCE"
export const NO_INSTANCE_AVAILABLE = "NO_INSTANCE_AVAILABLE"
export const REGION_NOT_ENABLED = "REGION_NOT_ENABLED"
export const USER_ALREADY_ACTIVE = "USER_ALREADY_ACTIVE"
export const AUTH_ERROR = "AUTH_ERROR"
export const NAVIGATION_ERROR = "NAVIGATION_ERROR"
export const MAINTENANCE_ERROR = "MAINTENANCE_ERROR"
export const INTERNET_ERROR = "INTERNET_ERROR"

export const whistError = {
  [NO_PAYMENT_ERROR]: {
    title: "Your free trial has expired!",
    text: "To continue receiving access to Whist, please sign up for a paid plan.",
  },
  [UNAUTHORIZED_ERROR]: {
    title: "There was an error authenticating you with Whist.",
    text: "Please try logging in again or contact support@whist.com for help.",
  },
  [PROTOCOL_ERROR]: {
    title: "The Whist browser lost connection.",
    text: "This could be due to inactivity or weak Internet. Please try again or contact support@whist.com for help.",
  },
  [MANDELBOX_INTERNAL_ERROR]: {
    title: "There was an error connecting to the Whist servers.",
    text: "Please check your Internet connection or try again in a few minutes.",
  },
  [AUTH_ERROR]: {
    title: "We've added extra security measures to our login system.",
    text: "Please sign out and sign back in. If this doesn't work, please contact support@whist.com to report a bug.",
  },
  [NAVIGATION_ERROR]: {
    title: "There was an error loading the Whist window.",
    text: "Please try logging in again or contact support@whist.com for help.",
  },
  [MAINTENANCE_ERROR]: {
    title: "Whist is currently pushing out an update.",
    text: "We apologize for the inconvenience. Please check back in a few minutes!",
  },
  [INTERNET_ERROR]: {
    title: "Please check your Internet connection.",
    text: "We were unable to ping our servers, which is likely a result of weak Internet.",
  },
  [COULD_NOT_LOCK_INSTANCE]: {
    title: "Whist encountered an unexpected database error :(",
    text: "We deeply apologize for the inconvenience. Retrying could cause this error to go away.",
  },
  [NO_INSTANCE_AVAILABLE]: {
    title: "Our servers are at capacity :(",
    text: "We deeply apologize for the inconvenience. We may have more capacity if you try again in a few moments.",
  },
  [REGION_NOT_ENABLED]: {
    title: "You are in a region that is not currently supported by Whist :(",
    text: "If you are located in North America, this may be our mistake. Please contact support@whist.com for help.",
  },
  [USER_ALREADY_ACTIVE]: {
    title: "You are connected to Whist on another device :(",
    text: "If you believe this is a mistake, we deeply apologize. Please contact support@whist.com for help.",
  },
  [COMMIT_HASH_MISMATCH]: {
    title: "Your Whist version is out of date :(",
    text: "Please allow a few seconds for an update to download. If nothing happens, we deeply apologize and ask you contact support@whist.com for help.",
  },
  [MANDELBOX_INTERNAL_ERROR]: {
    title: "There was an unexpected error with our servers :(",
    text: "This is likely a temporary bug and we deeply apologize for the inconvenience.",
  },
} as {
  [key: string]: {
    title: string
    text: string
  }
}
