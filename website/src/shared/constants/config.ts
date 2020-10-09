const development = {
    url: {
        PRIMARY_SERVER: "http://localhost:7300",
        FRONTEND_URL: "http://localhost:3000",
        GRAPHQL_HTTP_URL: "https://staging-database.tryfractal.com/v1/graphql",
        GRAPHQL_WS_URL: "wss://staging-database.tryfractal.com/v1/graphql",
    },
    keys: {
        STRIPE_PUBLIC_KEY: "pk_test_7y07LrJWC5LzNu17sybyn9ce004CLPaOXb",
    },
    sentry_env: "development",
}

const production = {
    url: {
        PRIMARY_SERVER: "https://main-webserver.tryfractal.com",
        FRONTEND_URL: "https://tryfractal.com",
        GRAPHQL_HTTP_URL: "https://prod-database.tryfractal.com/v1/graphql",
        GRAPHQL_WS_URL: "wss://prod-database.tryfractal.com/v1/graphql",
    },
    keys: {
        STRIPE_PUBLIC_KEY: "pk_live_XLjiiZB93KN0EjY8hwCxvKmB00whKEIj3U",
    },
    sentry_env: "production",
}

export const config =
    process.env.NODE_ENV === "development" ? development : production

export const GOOGLE_CLIENT_ID =
    "581514545734-7k820154jdfp0ov2ifk4ju3vodg0oec2.apps.googleusercontent.com"
