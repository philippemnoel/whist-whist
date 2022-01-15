// The session ID is a UNIX timestamp (milliseconds since epoch) that we use to track app
// launch times and also send to Amplitude and the host service to uniquely identify
// user sessions

export const sessionID = Date.now()

export const HEARTBEAT_INTERVAL_IN_MS = 5 * 60 * 1000
export const CHECK_UPDATE_INTERVAL_IN_MS = 5 * 60 * 1000

export const SENTRY_DSN =
  "https://5b0accb25f3341d280bb76f08775efe1@o400459.ingest.sentry.io/5412323"

export const MAX_URL_LENGTH = 2048

export const openSourceUrls = [
  "http://www.iet.unipi.it/~a007834/fec.html",
  "https://github.com/lvandeve/lodepng/blob/master/LICENSE",
]
