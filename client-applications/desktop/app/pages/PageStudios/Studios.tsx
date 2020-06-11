import React, { Component } from "react";
import { connect } from "react-redux";
import styles from "../PageDashboard/Dashboard.css";
import { history } from "store/configureStore";

import Titlebar from "react-electron-titlebar";
import Logo from "resources/images/logo.svg";
import WifiBox from "../PageDashboard/components/WifiBox";
import DistanceBox from "../PageDashboard/components/DistanceBox";
import CPUBox from "../PageDashboard/components/CPUBox";
import Typeform from "../PageDashboard/components/Typeform";
import MainBox from "../PageDashboard/components/MainBox";

import { Offline, Online } from "react-detect-offline";
import Popup from "reactjs-popup";

import {
    storeUsername,
    storeIP,
    askFeedback,
    changeWindow,
    pingIPInfo,
} from "actions/counter";

class Studios extends Component {
    constructor(props) {
        super(props);
        this.state = { isLoading: true, username: "", launches: 0, id: "" };
    }

    CloseWindow = () => {
        const remote = require("electron").remote;
        let win = remote.getCurrentWindow();

        win.close();
    };

    MinimizeWindow = () => {
        const remote = require("electron").remote;
        let win = remote.getCurrentWindow();

        win.minimize();
    };

    OpenFeedback = () => {
        this.props.dispatch(askFeedback(true));
    };

    OpenSettings = () => {
        this.props.dispatch(changeWindow("settings"));
    };

    ReturnToDashboard = () => {
        this.props.dispatch(changeWindow("main"));
    };

    LogOut = () => {
        this.props.dispatch(storeUsername(null));
        this.props.dispatch(storeIP(""));
        this.props.dispatch(storeIsUser(true));
        this.props.dispatch(fetchVMStatus(false));
        history.push("/");
    };

    componentDidMount() {
        var id = "0x123456789";
        this.props.dispatch(changeWindow("studios"));
        this.setState({ isLoading: false, id: id });
        this.props.dispatch(pingIPInfo(id));
    }

    componentDidUpdate(prevProps) {}

    render() {
        const barHeight = 5;

        return (
            <div
                className={styles.container}
                data-tid="container"
                style={{ fontFamily: "Maven Pro" }}
            >
                {this.props.os === "win32" ? (
                    <div>
                        <Titlebar backgroundColor="#000000" />
                    </div>
                ) : (
                    <div style={{ marginTop: 10 }}></div>
                )}
                {this.state.isLoading ? (
                    <div></div>
                ) : (
                    <div>
                        <Typeform />
                        <div className={styles.landingHeader}>
                            <div
                                className={styles.landingHeaderLeft}
                                onClick={this.ReturnToDashboard}
                            >
                                <img src={Logo} width="20" height="20" />
                                <span
                                    className={styles.logoTitle}
                                    style={{
                                        color: "#111111",
                                        fontWeight: "bold",
                                    }}
                                >
                                    Fractal
                                </span>
                            </div>
                            <div className={styles.landingHeaderRight}>
                                <span
                                    className={styles.headerButton}
                                    onClick={this.ReturnToDashboard}
                                >
                                    Home
                                </span>
                                <span
                                    className={styles.headerButton}
                                    onClick={this.OpenFeedback}
                                >
                                    Support
                                </span>
                                <span
                                    className={styles.headerButton}
                                    onClick={this.OpenSettings}
                                >
                                    Settings
                                </span>
                                <Popup
                                    trigger={
                                        <span className={styles.headerButton}>
                                            Refer a Friend
                                        </span>
                                    }
                                    modal
                                    contentStyle={{
                                        width: 300,
                                        borderRadius: 5,
                                        backgroundColor: "#111111",
                                        border: "none",
                                        height: 100,
                                        padding: 30,
                                        textAlign: "center",
                                    }}
                                >
                                    <div
                                        style={{
                                            fontWeight: "bold",
                                            fontSize: 20,
                                        }}
                                        className={styles.blueGradient}
                                    >
                                        <strong>Coming Soon</strong>
                                    </div>
                                    <div
                                        style={{
                                            fontSize: 12,
                                            color: "#D6D6D6",
                                            marginTop: 20,
                                        }}
                                    >
                                        Get rewarded when you refer a friend.
                                    </div>
                                </Popup>
                                <button
                                    onClick={this.LogOut}
                                    type="button"
                                    className={styles.signupButton}
                                    id="signup-button"
                                    style={{
                                        marginLeft: 25,
                                        fontFamily: "Maven Pro",
                                        fontWeight: "bold",
                                    }}
                                >
                                    Sign Out
                                </button>
                            </div>
                        </div>
                        <div style={{ display: "flex", padding: "20px 75px" }}>
                            <div
                                style={{
                                    width: "65%",
                                    textAlign: "left",
                                    paddingRight: 20,
                                }}
                            >
                                <MainBox
                                    currentWindow={this.props.currentWindow}
                                    default="studios"
                                    id={this.state.id}
                                />
                            </div>
                            {this.props.currentWindow === "studios" ? (
                                <div
                                    className={styles.statBox}
                                    style={{
                                        width: "35%",
                                        textAlign: "left",
                                        background:
                                            "linear-gradient(217.69deg, #363868 0%, rgba(30, 31, 66, 0.5) 101.4%)",
                                        borderRadius: 5,
                                        padding: 30,
                                        minHeight: 350,
                                    }}
                                >
                                    <div
                                        style={{
                                            fontWeight: "bold",
                                            fontSize: 20,
                                        }}
                                    >
                                        Welcome, {this.props.username}
                                    </div>
                                    <div
                                        style={{
                                            marginTop: 10,
                                            display: "inline-block",
                                        }}
                                    >
                                        <Online>
                                            <div
                                                style={{
                                                    background: "none",
                                                    border: "solid 1px #14a329",
                                                    height: 6,
                                                    width: 6,
                                                    borderRadius: 3,
                                                    display: "inline",
                                                    float: "left",
                                                    position: "relative",
                                                    top: 3.5,
                                                }}
                                            ></div>
                                            <div
                                                style={{
                                                    display: "inline",
                                                    float: "left",
                                                    marginLeft: 5,
                                                    fontSize: 12,
                                                    color: "#D6D6D6",
                                                }}
                                            >
                                                Online
                                            </div>
                                        </Online>
                                        <Offline>
                                            <div
                                                style={{
                                                    background: "none",
                                                    border: "solid 1px #14a329",
                                                    height: 6,
                                                    width: 6,
                                                    borderRadius: 3,
                                                    display: "inline",
                                                    float: "left",
                                                    position: "relative",
                                                    top: 3.5,
                                                }}
                                            ></div>
                                            <div
                                                style={{
                                                    display: "inline",
                                                    float: "left",
                                                    marginLeft: 5,
                                                    fontSize: 12,
                                                    color: "#D6D6D6",
                                                }}
                                            >
                                                Offline
                                            </div>
                                        </Offline>
                                    </div>

                                    <WifiBox barHeight={barHeight} />
                                    <DistanceBox
                                        barHeight={barHeight}
                                        public_ip={this.props.public_ip}
                                    />
                                    <CPUBox barHeight={barHeight} />
                                </div>
                            ) : (
                                <div></div>
                            )}
                        </div>
                    </div>
                )}
            </div>
        );
    }
}

function mapStateToProps(state: {
    counter: {
        username: any;
        public_ip: any;
        os: any;
        askFeedback: any;
        window: any;
        ipInfo: any;
    };
}) {
    return {
        username: state.counter.username,
        public_ip: state.counter.public_ip,
        os: state.counter.os,
        askFeedback: state.counter.askFeedback,
        currentWindow: state.counter.window,
        ipInfo: state.counter.ipInfo,
    };
}

export default connect(mapStateToProps)(Studios);
