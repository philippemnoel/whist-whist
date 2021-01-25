import React, { Fragment } from "react"
import { render } from "react-dom"
import { Provider } from "react-redux"
import { ConnectedRouter } from "connected-react-router"
import { AppContainer as ReactHotAppContainer } from "react-hot-loader"
import {
    ApolloProvider,
    ApolloClient,
    InMemoryCache,
    HttpLink,
    split,
} from "@apollo/client"
import { getMainDefinition } from "@apollo/client/utilities"
import { WebSocketLink } from "@apollo/client/link/ws"

import Root from "pages/root"

import { config } from "shared/constants/config"
import configureStore from "store/configureStore"
import { history } from "store/history"

import "app.global.css"

// set up Sentry - import proper init based on running thread
const { init } =
    process.type === "browser"
        ? require("@sentry/electron/dist/main")
        : require("@sentry/electron/dist/renderer")

if (process.env.NODE_ENV === "production") {
    init({
        dsn:
            "https://5b0accb25f3341d280bb76f08775efe1@o400459.ingest.sentry.io/5412323",
        environment: config.sentryEnv,
        release: "client-applications@1.0.0", // TODO: make this the real release version
    })
}

const store = configureStore()

const AppContainer = process.env.PLAIN_HMR ? Fragment : ReactHotAppContainer

// Set up Apollo GraphQL provider for https and wss (websocket)

const httpLink = new HttpLink({
    uri: config.url.GRAPHQL_HTTP_URL,
})

const wsLink = new WebSocketLink({
    uri: config.url.GRAPHQL_WS_URL,
    options: {
        reconnect: true,
    },
})

const splitLink = split(
    ({ query }) => {
        const definition = getMainDefinition(query)
        return (
            definition.kind === "OperationDefinition" &&
            definition.operation === "subscription"
        )
    },
    wsLink,
    httpLink
)

const apolloClient = new ApolloClient({
    link: splitLink,
    cache: new InMemoryCache(),
})

document.addEventListener("DOMContentLoaded", () =>
    render(
        <AppContainer>
            <Provider store={store}>
                <ConnectedRouter history={history}>
                    <ApolloProvider client={apolloClient}>
                        <Root />
                    </ApolloProvider>
                </ConnectedRouter>
            </Provider>
        </AppContainer>,
        document.getElementById("root")
    )
)
