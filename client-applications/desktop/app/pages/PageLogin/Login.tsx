import React, { Component } from "react";
import { connect } from "react-redux";

import styles from "pages/PageLogin/Login.css";
import Titlebar from "react-electron-titlebar";
import Background from "resources/images/background.jpg";
import Logo from "resources/images/logo.svg";
import UpdateScreen from "pages/PageDashboard/components/UpdateScreen.tsx";

import { FontAwesomeIcon } from "@fortawesome/react-fontawesome";
import {
    faCircleNotch,
    faUser,
    faLock,
} from "@fortawesome/free-solid-svg-icons";

import { FaGoogle } from "react-icons/fa";

import {
    loginUser,
    loginStudio,
    loginFailed,
    googleLogin,
} from "actions/counter";

import { GOOGLE_CLIENT_ID } from "constants/config.ts";

class Login extends Component {
    constructor(props) {
        super(props);
        this.state = {
            username: "",
            password: "",
            loggingIn: false,
            warning: false,
            version: "1.0.0",
            studios: false,
            rememberMe: false,
            update_ping_received: false,
            needs_autoupdate: false,
            maintenance: true,
        };
    }

    CloseWindow = () => {
        const { remote } = require("electron");
        const win = remote.getCurrentWindow();

        win.close();
    };

    MinimizeWindow = () => {
        const { remote } = require("electron");
        const win = remote.getCurrentWindow();

        win.minimize();
    };

    UpdateUsername = (evt: any) => {
        this.setState({
            username: evt.target.value,
        });
    };

    UpdatePassword = (evt: any) => {
        this.setState({
            password: evt.target.value,
        });
    };

    LoginUser = () => {
        const storage = require("electron-json-storage");
        this.props.dispatch(loginFailed(false));
        this.setState({ loggingIn: true });
        if (this.state.rememberMe) {
            storage.set("credentials", {
                username: this.state.username,
                password: this.state.password,
            });
        } else {
            storage.set("credentials", { username: "", password: "" });
        }
        this.props.dispatch(
            loginUser(this.state.username.trim(), this.state.password)
        );
    };

    LoginKeyPress = (event: any) => {
        if (event.key === "Enter" && !this.state.studios) {
            this.LoginUser();
        }
        if (event.key === "Enter" && this.state.studios) {
            // this.LoginStudio()
        }
    };

    GoogleLogin = () => {
        const { BrowserWindow } = require("electron").remote;

        const authWindow = new BrowserWindow({
            width: 800,
            height: 600,
            show: false,
            "node-integration": false,
            "web-security": false,
        });
        const authUrl = `https://accounts.google.com/o/oauth2/v2/auth?scope=openid%20profile%20email&openid.realm&include_granted_scopes=true&response_type=code&redirect_uri=urn:ietf:wg:oauth:2.0:oob:auto&client_id=${GOOGLE_CLIENT_ID}&origin=https%3A//fractalcomputers.com`;
        authWindow.loadURL(authUrl, { userAgent: "Chrome" });
        authWindow.show();

        authWindow.webContents.on("page-title-updated", (event, newUrl) => {
            const pageTitle = authWindow.getTitle();
            if (pageTitle.includes("Success")) {
                const codeRegexp = new RegExp(
                    "^(?:Success code=)(.+?)(?:&.+)$"
                );
                const code = pageTitle.match(codeRegexp)[1];
                this.setState({ loggingIn: true });
                this.props.dispatch(googleLogin(code));
            }
        });
    };

    ForgotPassword = () => {
        const { shell } = require("electron");
        shell.openExternal("https://www.fractalcomputers.com/reset");
    };

    SignUp = () => {
        const { shell } = require("electron");
        shell.openExternal("https://www.fractalcomputers.com/auth");
    };

    CloseWindow = () => {
        const { remote } = require("electron");
        const win = remote.getCurrentWindow();

        win.close();
    };

    MinimizeWindow = () => {
        const { remote } = require("electron");
        const win = remote.getCurrentWindow();

        win.minimize();
    };

    ToggleStudio = (isStudio: any) => {
        this.setState({ studios: isStudio });
    };

    LoginStudio = () => {
        this.setState({ loggingIn: true, warning: false });
        this.props.dispatch(
            loginStudio(this.state.username, this.state.password)
        );
    };

    changeRememberMe = (event: any) => {
        const { target } = event;
        if (target.checked) {
            this.setState({ rememberMe: true });
        } else {
            this.setState({ rememberMe: false });
        }
    };

    componentDidMount() {
        const ipc = require("electron").ipcRenderer;

        ipc.on("update", (event, update) => {
            component.setState({
                update_ping_received: true,
                needs_autoupdate: update,
            });
        });

        const appVersion = require("../../package.json").version;
        this.setState({ version: appVersion });
    }

    render() {
        if (this.state.maintenance) {
            return (
                <div
                    className={styles.container}
                    data-tid="container"
                    style={{ backgroundImage: `url(${Background})` }}
                >
                    <UpdateScreen />
                    <div
                        style={{
                            position: "absolute",
                            bottom: 15,
                            right: 15,
                            fontSize: 11,
                            color: "#D1D1D1",
                        }}
                    >
                        Version: {this.state.version}
                    </div>
                    {this.props.os === "win32" ? (
                        <div>
                            <Titlebar backgroundColor="#000000" />
                        </div>
                    ) : (
                        <div className={styles.macTitleBar} />
                    )}
                    <div
                        style={{
                            margin: "auto",
                            marginTop: 250,
                            color: "white",
                            width: 600,
                            lineHeight: 1.5,
                        }}
                    >
                        Fractal is undergoing a major update, and is currently
                        in maintenance mode. If you need support or would like
                        to access your account, please contact{" "}
                        <strong>support@fractalcomputers.com</strong>.
                    </div>
                </div>
            );
        }
        return (
            <div
                className={styles.container}
                data-tid="container"
                style={{ backgroundImage: `url(${Background})` }}
            >
                <UpdateScreen />
                <div
                    style={{
                        position: "absolute",
                        bottom: 15,
                        right: 15,
                        fontSize: 11,
                        color: "#D1D1D1",
                    }}
                >
                    Version: {this.state.version}
                </div>
                {this.props.os === "win32" ? (
                    <div>
                        <Titlebar backgroundColor="#000000" />
                    </div>
                ) : (
                    <div className={styles.macTitleBar} />
                )}
                <div className={styles.removeDrag}>
                    <div className={styles.landingHeader}>
                        <div className={styles.landingHeaderLeft}>
                            <img src={Logo} width="18" height="18" />
                            <span className={styles.logoTitle}>Fractal</span>
                        </div>
                        <div className={styles.landingHeaderRight}>
                            <span
                                id="forgotButton"
                                onClick={this.ForgotPassword}
                            >
                                Forgot Password?
                            </span>
                            <button
                                type="button"
                                className={styles.signupButton}
                                style={{ borderRadius: 5, marginLeft: 15 }}
                                id="signup-button"
                                onClick={this.SignUp}
                            >
                                Sign Up
                            </button>
                        </div>
                    </div>
                    <div style={{ marginTop: this.props.warning ? 10 : 60 }}>
                        {this.state.studios ? (
                            <div
                                style={{
                                    margin: "auto",
                                    display: "flex",
                                    alignItems: "center",
                                    justifyContent: "center",
                                }}
                            >
                                <div
                                    className={styles.tabHeader}
                                    onClick={() => this.ToggleStudio(false)}
                                    style={{
                                        color: "#DADADA",
                                        marginRight: 20,
                                        paddingBottom: 8,
                                        width: 90,
                                    }}
                                >
                                    Personal
                                </div>
                                <div
                                    className={styles.tabHeader}
                                    onClick={() => this.ToggleStudio(true)}
                                    style={{
                                        color: "white",
                                        fontWeight: "bold",
                                        marginLeft: 20,
                                        borderBottom: "solid 3px #5EC4EB",
                                        paddingBottom: 8,
                                        width: 90,
                                    }}
                                >
                                    Teams
                                </div>
                            </div>
                        ) : (
                            <div
                                style={{
                                    margin: "auto",
                                    display: "flex",
                                    alignItems: "center",
                                    justifyContent: "center",
                                }}
                            >
                                <div
                                    className={styles.tabHeader}
                                    onClick={() => this.ToggleStudio(false)}
                                    style={{
                                        color: "white",
                                        fontWeight: "bold",
                                        marginRight: 20,
                                        borderBottom: "solid 3px #5EC4EB",
                                        paddingBottom: 8,
                                        width: 90,
                                    }}
                                >
                                    Personal
                                </div>
                                <div
                                    className={styles.tabHeader}
                                    onClick={() => this.ToggleStudio(true)}
                                    style={{
                                        color: "#DADADA",
                                        marginLeft: 20,
                                        paddingBottom: 8,
                                        width: 90,
                                    }}
                                >
                                    Teams
                                </div>
                            </div>
                        )}
                        <div className={styles.loginContainer}>
                            <div>
                                <FontAwesomeIcon
                                    icon={faUser}
                                    style={{
                                        color: "white",
                                        fontSize: 12,
                                    }}
                                    className={styles.inputIcon}
                                />
                                <input
                                    onKeyPress={this.LoginKeyPress}
                                    onChange={this.UpdateUsername}
                                    type="text"
                                    className={styles.inputBox}
                                    style={{ borderRadius: 5 }}
                                    placeholder="Username"
                                    id="username"
                                />
                            </div>
                            <div>
                                <FontAwesomeIcon
                                    icon={faLock}
                                    style={{
                                        color: "white",
                                        fontSize: 12,
                                    }}
                                    className={styles.inputIcon}
                                />
                                <input
                                    onKeyPress={this.LoginKeyPress}
                                    onChange={this.UpdatePassword}
                                    type="password"
                                    className={styles.inputBox}
                                    style={{ borderRadius: 5 }}
                                    placeholder="Password"
                                    id="password"
                                />
                            </div>
                            <div style={{ marginBottom: 20 }}>
                                {!this.state.studios ? (
                                    this.state.loggingIn &&
                                    !this.props.warning ? (
                                        <button
                                            type="button"
                                            className={styles.loginButton}
                                            id="login-button"
                                            style={{
                                                opacity: 0.6,
                                                textAlign: "center",
                                            }}
                                        >
                                            <FontAwesomeIcon
                                                icon={faCircleNotch}
                                                spin
                                                style={{
                                                    color: "white",
                                                    width: 12,
                                                    marginRight: 5,
                                                    position: "relative",
                                                    top: 0.5,
                                                }}
                                            />{" "}
                                            Processing
                                        </button>
                                    ) : (
                                        <button
                                            onClick={() => this.LoginUser()}
                                            type="button"
                                            className={styles.loginButton}
                                            id="login-button"
                                        >
                                            START
                                        </button>
                                    )
                                ) : (
                                    <button
                                        type="button"
                                        className={styles.loginButton}
                                        id="login-button"
                                        style={{
                                            opacity: 0.5,
                                            background:
                                                "linear-gradient(258.54deg, #5ec3eb 0%, #5ec3eb 100%)",
                                            borderRadius: 5,
                                        }}
                                    >
                                        Get Started
                                    </button>
                                )}
                                <div style={{ marginBottom: 20 }}>
                                    {this.state.loggingIn &&
                                    !this.props.warning ? (
                                        <button
                                            type="button"
                                            className={styles.googleButton}
                                            id="google-button"
                                            style={{
                                                opacity: 0.6,
                                                textAlign: "center",
                                            }}
                                        >
                                            <FontAwesomeIcon
                                                icon={faCircleNotch}
                                                spin
                                                style={{
                                                    color: "white",
                                                    width: 12,
                                                    marginRight: 5,
                                                    position: "relative",
                                                    top: 0.5,
                                                }}
                                            />{" "}
                                            Processing
                                        </button>
                                    ) : (
                                        <button
                                            onClick={() => this.GoogleLogin()}
                                            type="button"
                                            className={styles.googleButton}
                                            id="google-button"
                                        >
                                            <FaGoogle
                                                style={{
                                                    fontSize: 16,
                                                    marginRight: 10,
                                                    position: "relative",
                                                    top: 3,
                                                }}
                                            />
                                            Login with Google
                                        </button>
                                    )}
                                </div>
                            </div>
                            {this.props.warning && (
                                <div
                                    style={{
                                        textAlign: "center",
                                        fontSize: 12,
                                        color: "#f9000b",
                                        background: "rgba(253, 240, 241, 0.9)",
                                        width: "100%",
                                        padding: 15,
                                        borderRadius: 2,
                                        margin: "auto",
                                        marginBottom: 30,
                                        width: 265,
                                    }}
                                >
                                    <div>
                                        Invalid credentials. If you lost your
                                        password, you can reset it on the&nbsp;
                                        <div
                                            onClick={this.ForgotPassword}
                                            className={styles.pointerOnHover}
                                            style={{
                                                display: "inline",
                                                fontWeight: "bold",
                                                textDecoration: "underline",
                                            }}
                                        >
                                            website
                                        </div>
                                        .
                                    </div>
                                </div>
                            )}
                            <div
                                style={{
                                    marginTop: 25,
                                    display: "flex",
                                    justifyContent: "center",
                                    alignItems: "center",
                                }}
                            >
                                <label className={styles.termsContainer}>
                                    <input
                                        type="checkbox"
                                        onChange={this.changeRememberMe}
                                        onKeyPress={this.LoginKeyPress}
                                    />
                                    <span className={styles.checkmark} />
                                </label>

                                <div style={{ fontSize: 12 }}>Remember Me</div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        );
    }
}

function mapStateToProps(state) {
    return {
        username: state.counter.username,
        public_ip: state.counter.public_ip,
        warning: state.counter.warning,
        os: state.counter.os,
    };
}

export default connect(mapStateToProps)(Login);
