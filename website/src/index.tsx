import React from "react"
import ReactDOM from "react-dom"
import { Provider } from "react-redux"
import { applyMiddleware, createStore } from "redux"
import createSagaMiddleware from "redux-saga"
import { persistStore, persistReducer } from "redux-persist"
import { routerMiddleware } from "connected-react-router"
import { Router } from "react-router"
import { PersistGate } from "redux-persist/integration/react"
import { composeWithDevTools } from "redux-devtools-extension"
import ReduxPromise from "redux-promise"
import storage from "redux-persist/lib/storage"
import rootSaga from "store/sagas/root"
import * as Sentry from "@sentry/react"
import { ApolloProvider } from "@apollo/react-hooks"
import { ApolloClient, InMemoryCache, HttpLink, split } from "@apollo/client"
import { getMainDefinition } from "@apollo/client/utilities"
import { WebSocketLink } from "@apollo/client/link/ws"
import { loadStripe } from "@stripe/stripe-js"
import { Elements } from "@stripe/react-stripe-js"

import history from "shared/utils/history"
import { MainProvider } from "shared/context/mainContext"
import { config } from "shared/constants/config"
import rootReducer from "store/reducers/root"
import * as serviceWorker from "serviceWorker"
import { STRIPE_OPTIONS } from "shared/constants/stripe"

import "styles/shared.css"
import "bootstrap/dist/css/bootstrap.min.css"

import RootApp from "rootApp"

if (process.env.REACT_APP_ENVIRONMENT === "production") {
    // the netlify build command is
    //   export REACT_APP_VERSION=$COMMIT_HASH && npm run build
    // so this environment variable should be set on netlify deploys
    let gitHash = "local"
    if (process.env.REACT_APP_VERSION) {
        gitHash = process.env.REACT_APP_VERSION
    }
    Sentry.init({
        dsn:
            "https://4fbefcae900443d58c38489898773eea@o400459.ingest.sentry.io/5394481",
        environment: config.sentry_env,
        release: "website@" + gitHash,
    })
}

const sentryReduxEnhancer = Sentry.createReduxEnhancer({})

const persistConfig = {
    key: "rootKey",
    storage,
}

const sagaMiddleware = createSagaMiddleware()
const persistedReducer = persistReducer(persistConfig, rootReducer)

let middleware = [routerMiddleware(history), ReduxPromise, sagaMiddleware]

const store = createStore(
    persistedReducer,
    composeWithDevTools(applyMiddleware(...middleware), sentryReduxEnhancer)
)

const persistor = persistStore(store)

sagaMiddleware.run(rootSaga)

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

const stripePromise = loadStripe(config.keys.STRIPE_PUBLIC_KEY)

ReactDOM.render(
    <React.StrictMode>
        <Sentry.ErrorBoundary fallback={"An error has occurred"}>
            <Router history={history}>
                <Provider store={store}>
                    <PersistGate loading={null} persistor={persistor}>
                        <ApolloProvider client={apolloClient}>
                            <Elements
                                stripe={stripePromise}
                                options={STRIPE_OPTIONS}
                            >
                                <MainProvider>
                                    <RootApp />
                                </MainProvider>
                            </Elements>
                        </ApolloProvider>
                    </PersistGate>
                </Provider>
            </Router>
        </Sentry.ErrorBoundary>
    </React.StrictMode>,
    document.getElementById("root")
)

// If you want your app to work offline and load faster, you can change
// unregister() to register() below. Note this comes with some pitfalls.
// Learn more about service workers: https://bit.ly/CRA-PWA
serviceWorker.unregister()
