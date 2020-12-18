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
        statusID: null,
        pngFile: null,
    },
    client: {
        clientOS: null,
        region: null,
        dpi: null,
        apps: [],
        onboardApps: [],
    },
    payment: {
        accountLocked: false,
        promoCode: null,
    },
    apps: {
        externalApps: [], // all external applications (example entry: {code_name: "google_drive", display_name: "Google Drive", ...})
        connectedApps: [], // all external application ids that the user has connected their account to
        authenticated: null, // app name that has successfully authenticated
        disconnected: null, // app name that has been successfully disconnected
        disconnectWarning: null, // app name that tried to disconnect but failed
    },
    admin: {
        webserverUrl: "dev",
        taskArn: "fractal-browsers-chrome",
        region: "us-east-1",
        cluster: null,
        launched: false,
    },
}

// default export until we have multiple exports
export default DEFAULT
