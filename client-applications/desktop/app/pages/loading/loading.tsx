import React, { useEffect, useState, Dispatch } from "react"
import { connect } from "react-redux"
import { useSpring, animated } from "react-spring"
import { FontAwesomeIcon } from "@fortawesome/react-fontawesome"
import { faCircleNotch } from "@fortawesome/free-solid-svg-icons"

import TitleBar from "shared/components/titleBar"
import { debugLog } from "shared/utils/general/logging"
import { updateContainer, updateLoading } from "store/actions/pure"
import { history } from "store/history"
import { execPromise } from "shared/utils/files/exec"
import { FractalRoute } from "shared/types/navigation"
import { OperatingSystem, FractalDirectory } from "shared/types/client"
import { FractalClientCache } from "shared/types/cache"

import styles from "pages/loading/loading.css"

const Loading = (props: {
    percentLoaded: number
    status: string
    port32262: number
    port32263: number
    port32273: number
    ip: string
    secretKey: string
    desiredAppID: string
    currentAppID: string
    containerID: string
    pngFile: string
    dispatch: Dispatch<any>
}) => {
    const {
        percentLoaded,
        status,
        port32262,
        port32263,
        port32273,
        ip,
        secretKey,
        desiredAppID,
        currentAppID,
        containerID,
        pngFile,
        dispatch,
    } = props

    // figure out how to use useEffect
    // note to future developers: setting state inside useffect when you rely on
    // change for those variables to trigger runs forever and is bad
    // use two variables for that or instead do something like this below
    const percentLoadedWidth = 5 * percentLoaded

    const [launches, setLaunches] = useState(0)
    const loadingBar = useSpring({ width: percentLoadedWidth })

    const resetLaunchRedux = () => {
        dispatch(
            updateContainer({
                containerID: null,
                cluster: null,
                port32262: null,
                port32263: null,
                port32273: null,
                publicIP: null,
                secretKey: null,
                launches: 0,
                launchURL: null,
            })
        )
        dispatch(
            updateLoading({
                statusMessage: "Powering up your app",
                percentLoaded: 0,
            })
        )
    }

    const getExecutableName = (): string => {
        const currentOS = require("os").platform()

        if (currentOS === OperatingSystem.MAC) {
            return "./FractalClient"
        }
        if (currentOS === OperatingSystem.WINDOWS) {
            return "FractalClient.exe"
        }
        debugLog(`no suitable os found, instead got ${currentOS}`)
        return ""
    }

    const launchProtocol = () => {
        const Store = require("electron-store")
        const storage = new Store()

        const cachedLowInternetMode = storage.get(
            FractalClientCache.LOW_INTERNET_MODE
        )
        const internetMode = cachedLowInternetMode ? "h265" : "h264"
        const cachedBandwidth = storage.get(FractalClientCache.BANDWIDTH)

        const spawn = require("child_process").spawn

        const protocolPath = require("path").join(
            FractalDirectory.getRootDirectory(),
            "protocol-build/desktop"
        )
        const executable = getExecutableName()

        execPromise("chmod +x FractalClient", protocolPath, [
            OperatingSystem.MAC,
            OperatingSystem.LINUX,
        ])
            .then(() => {
                const ipc = require("electron").ipcRenderer
                ipc.sendSync("canClose", false)

                const portInfo = `32262:${port32262}.32263:${port32263}.32273:${port32273}`
                const protocolParameters = {
                    width: 800,
                    height: 600,
                    codec: internetMode,
                    ports: portInfo,
                    "private-key": secretKey,
                    name: `Fractalized ${desiredAppID}`,
                    ...(bitrate && { bitrate: cachedBandwidth }),
                    ...(pngFile && { icon: pngFile }),
                }

                const protocolArguments = [
                    ...Object.entries(protocolParameters)
                        .map(([flag, arg]) => [`--${flag}`, arg])
                        .flat(),
                    ip,
                ]

                // Starts the protocol
                const protocol = spawn(executable, protocolArguments, {
                    cwd: protocolPath,
                    detached: false,
                    stdio: "ignore",
                })
                protocol.on("close", () => {
                    resetLaunchRedux()
                    setLaunches(0)
                    ipc.sendSync("canClose", true)
                    history.push(FractalRoute.DASHBOARD)
                })
                return null
            })
            .catch((error) => {
                throw error
            })
    }

    const returnToDashboard = () => {
        resetLaunchRedux()
        setLaunches(0)
        history.push(FractalRoute.DASHBOARD)
    }

    // TODO (adriano) graceful exit vs non graceful exit code
    // this should be done AFTER the endpoint to connect to EXISTS

    useEffect(() => {
        // Ensures that a container exists, that the protocol has not been launched before, and that
        // the app we want to launch is the app that will be launched
        if (containerID && launches === 0 && currentAppID === desiredAppID) {
            setLaunches(launches + 1)
        }
    }, [containerID])

    useEffect(() => {
        if (launches === 1) {
            launchProtocol()
        }
    }, [launches])

    return (
        <div
            style={{
                position: "fixed",
                top: 0,
                left: 0,
                width: 1000,
                height: 680,
                zIndex: 1000,
            }}
        >
            <TitleBar />
            <div className={styles.landingHeader}>
                <div className={styles.landingHeaderLeft}>
                    <span className={styles.logoTitle}>Fractal</span>
                </div>
            </div>
            <div style={{ position: "relative" }}>
                <div
                    style={{
                        paddingTop: 250,
                        textAlign: "center",
                        color: "white",
                        width: 1000,
                    }}
                >
                    <div
                        style={{
                            width: "500px",
                            margin: "auto",
                            background: "#cccccc",
                            height: "6px",
                        }}
                    >
                        <animated.div
                            style={loadingBar}
                            className={styles.loadingBar}
                        />
                    </div>
                    <div
                        style={{
                            marginTop: 10,
                            fontSize: 14,
                            display: "flex",
                            alignItems: "center",
                            justifyContent: "center",
                        }}
                    >
                        <div style={{ display: "flex", color: "#333333" }}>
                            {percentLoaded !== 100 && (
                                <FontAwesomeIcon
                                    icon={faCircleNotch}
                                    spin
                                    style={{
                                        color: "#333333",
                                        marginRight: 10,
                                        width: 12,
                                        position: "relative",
                                        top: 3,
                                    }}
                                />
                            )}{" "}
                            {status}
                        </div>
                    </div>
                    <div style={{ display: "flex", justifyContent: "center" }}>
                        {status && // a little bit hacky, but gets the job done
                            typeof status === "string" &&
                            status.toLowerCase().includes("unexpected") && (
                                <div
                                    role="button" // so eslint will not yell
                                    tabIndex={0} // also for eslint
                                    className={styles.dashboardButton}
                                    style={{ width: 220, marginTop: 25 }}
                                    onClick={returnToDashboard}
                                    onKeyDown={returnToDashboard} // eslint
                                >
                                    BACK TO DASHBOARD
                                </div>
                            )}
                    </div>
                </div>
            </div>
        </div>
    )
}

const mapStateToProps = (state: { MainReducer: Record<string, any> }) => {
    return {
        percentLoaded: state.MainReducer.loading.percentLoaded,
        status: state.MainReducer.loading.statusMessage,
        containerID: state.MainReducer.container.containerID,
        cluster: state.MainReducer.container.cluster,
        port32262: state.MainReducer.container.port32262,
        port32263: state.MainReducer.container.port32263,
        port32273: state.MainReducer.container.port32273,
        location: state.MainReducer.container.location,
        ip: state.MainReducer.container.publicIP,
        secretKey: state.MainReducer.container.secretKey,
        desiredAppID: state.MainReducer.container.desiredAppID,
        currentAppID: state.MainReducer.container.currentAppID,
        pngFile: state.MainReducer.container.pngFile,
    }
}

export default connect(mapStateToProps)(Loading)
