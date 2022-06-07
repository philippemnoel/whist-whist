/* config.js
 *
 * Retrieves the configuration for the specified Auth0 tenant.
 *
 * To be called via:
 * yarn deploy:[env]
 */

// These don't need to be secrets, they're public values.
const clientIDs = {
  dev: "6cd4nskyIHQOePVM6q7FU3x7i5sZLwl1",
  staging: "2VZTg2ZT1DNUr6JquMfeezRXCc8V1nC5",
  prod: "dnhVmqdHkF1O6aWwakv7jDVMd5Ii6VfX",
}

const getConfig = (env) => {
  // List of environment variables that must be defined in order for deploy to succeed.
  const REQUIRED_ENV_VARS = [
    "AUTH0_CLIENT_SECRET",
    "GOOGLE_OAUTH_SECRET",
    "APPLE_OAUTH_SECRET",
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
    AUTH0_ALLOW_DELETE: env === "dev",
    AUTH0_KEYWORD_REPLACE_MAPPINGS: {
      ALLOWED_AUTH0_CALLBACKS: ["http://localhost/callback"],
      WHIST_AUTHENTICATION_API: "https://api.fractal.co",
      GOOGLE_OAUTH_SECRET: process.env.GOOGLE_OAUTH_SECRET,
      APPLE_OAUTH_SECRET: process.env.APPLE_OAUTH_SECRET,
    },
    EXCLUDED_PROPS: {
      clients: ["client_secret"],
    },
  }
}

module.exports = {
  getConfig,
}
