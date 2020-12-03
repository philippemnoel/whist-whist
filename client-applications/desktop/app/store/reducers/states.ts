export const DEFAULT = {
    auth: {
        username: null,
        candidateAccessToken: null,
        accessToken: null,
        refreshToken: null,
        loginWarning: false,
        loginMessage: null,
        name: null,
    },
    container: {
        publicIP: null,
        containerID: null,
        cluster: null,
        port32262: null,
        port32263: null,
        port32273: null,
        location: null,
        secretKey: null,
        currentAppID: null,
        desiredAppID: null,
        launches: 0,
        launchURL: null,
    },
    client: {
        clientOS: null,
        region: null,
        dpi: null,
    },
    payment: {
        accountLocked: false,
        promoCode: null,
    },
    loading: {
        statusMessage: "Powering up your app",
        percentLoaded: 0,
    },
    apps: {
        notInstalled: [],
        installing: [],
        installed: [],
    },
    admin: {
        webserver_url: "dev",
        task_arn: "fractal-browers-firefox",
        region: "us-east-1",
        cluster: null,
    },
}

// default export until we have multiple exports
export default DEFAULT
