/*
    All environment variables.
*/
const configs = {
  DEVELOPMENT: { // LOCAL would be the same as DEVELOPMENT here, so we omit it
    urls: {
      SCALING_SERVICE_URL: "https://dev-scaling-service.whist.com",
      CLIENT_CALLBACK_URL: "http://localhost/callback",
    },
    auth0: {
      AUTH_DOMAIN_URL: "fractal-dev.us.auth0.com",
      AUTH_CLIENT_ID: "jqL5eGNERea0JKWAFQN94ggmEnNQNW1j", // public; not a secret
      AUTH_API_IDENTIFIER: "https://api.fractal.co",
    },
  },
  STAGING: {
    urls: {
      SCALING_SERVICE_URL: "https://staging-scaling-service.whist.com",
      CLIENT_CALLBACK_URL: "http://localhost/callback",
    },
    auth0: {
      AUTH_DOMAIN_URL: "fractal-staging.us.auth0.com",
      AUTH_CLIENT_ID: "WXO2cphPECuDc7DkDQeuQzYUtCR3ehjz", // public; not a secret
      AUTH_API_IDENTIFIER: "https://api.fractal.co",
    },
  },
  PRODUCTION: {
    urls: {
      SCALING_SERVICE_URL: "https://prod-scaling-service.whist.com",
      CLIENT_CALLBACK_URL: "http://localhost/callback",
    },
    auth0: {
      AUTH_DOMAIN_URL: "auth.whist.com",
      AUTH_CLIENT_ID: "Ulk5B2RfB7mM8BVjA3JtkrZT7HhWIBLD", // public; not a secret
      AUTH_API_IDENTIFIER: "https://api.fractal.co",
    },
  },
}

export const config = (() => {
  // This value gets set by the client application at build time.
  // Acceptable values: local|dev|staging|prod
  const appEnvironment = process.env.CORE_TS_ENVIRONMENT as string
  switch (appEnvironment) {
    case "local":
    case "dev":
      return configs.DEVELOPMENT
    case "staging":
      return configs.STAGING
    case "prod":
      return configs.PRODUCTION
    default:
      console.warn(
        `Got an unrecognized ENVIRONMENT: ${appEnvironment}. Defaulting to "DEVELOPMENT"}`
      )
      return configs.DEVELOPMENT
  }
})()
