/* config.js
 *
 * Retrieves the configuration for the specified Auth0 tenant.
 *
 * To be called via:
 * yarn deploy:[env]
 */

// These don't need to be secrets, they're public values.
const clientIDs = {
  dev: "zfNXAien2yNOJBKaxcVu7ngWfwr6l2eP",
  staging: "anDOA3nAkJEPnE2YwwUY0MYnJRaXxvcV",
  prod: "K60rqZ4sqUJaXUol4SOjysPUh7OjayQE",
}

const getConfig = (env) => {
  // List of environment variables that must be defined in order for deploy to succeed.
  const REQUIRED_ENV_VARS = [
    "AUTH0_CLIENT_SECRET",
    "GOOGLE_OAUTH_SECRET",
  ]

  REQUIRED_ENV_VARS.forEach((v) => {
    if (!process.env[v]) {
      console.error(`Environment variable "${v}" not defined`)
      process.exit(1)
    }
  })

  return {
    AUTH0_DOMAIN: `fractal-${env}.us.auth0.com`,
    AUTH0_CLIENT_ID: clientIDs[env],
    AUTH0_CLIENT_SECRET: process.env.AUTH0_CLIENT_SECRET,
    AUTH0_BASE_PATH: "src",
    // Only auto-delete resources on dev
    AUTH0_ALLOW_DELETE: env === "dev" && false,
    AUTH0_KEYWORD_REPLACE_MAPPINGS: {
      GOOGLE_OAUTH_SECRET: process.env.GOOGLE_OAUTH_SECRET,
    },
    EXCLUDED_PROPS: {
      clients: ["client_secret"],
    },
  }
}

module.exports = {
  getConfig,
}
