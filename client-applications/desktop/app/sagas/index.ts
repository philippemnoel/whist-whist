import { put, takeEvery, all, call, select, delay } from "redux-saga/effects";
import { apiPost, apiGet } from "utils/api";
import * as Action from "actions/counter";
import { history } from "store/configureStore";

import { config } from "constants/config";

function* refreshAccess(action) {
    const state = yield select();
    const { json, response } = yield call(
        apiPost,
        `${config.url.PRIMARY_SERVER}/token/refresh`,
        {},
        state.counter.refresh_token
    );
    if (json) {
        yield put(Action.storeJWT(json.access_token, json.refresh_token));
    }
}

function* googleLogin(action) {
    yield select();

    if (action.code) {
        const { json } = yield call(
            apiPost,
            `${config.url.PRIMARY_SERVER}/account/googleLogin`,
            {
                code: action.code,
                clientApp: true,
            }
        );
        if (json) {
            if (json.status === 200) {
                yield put(Action.fetchDisk(json.username));
                yield call(fetchPaymentInfo, { username: json.username });

                yield put(Action.storeUsername(json.username));
                yield put(Action.storeIsUser(json.is_user));
                yield put(
                    Action.storeJWT(json.access_token, json.refresh_token)
                );

                yield call(getPromoCode, { username: json.username });

                history.push("/dashboard");
            } else {
                yield put(Action.loginFailed(true));
            }
        }
    } else {
        yield put(Action.loginFailed(true));
    }
}

function* loginUser(action) {
    if (action.username !== "" && action.password !== "") {
        const { json, response } = yield call(
            apiPost,
            `${config.url.PRIMARY_SERVER}/account/login`,
            {
                username: action.username,
                password: action.password,
            }
        );

        if (json && json.verified) {
            yield put(Action.fetchDisk(action.username));
            yield call(fetchPaymentInfo, action);
            yield put(Action.storeUsername(action.username));
            yield put(Action.storeIsUser(json.is_user));
            yield put(Action.storeJWT(json.access_token, json.refresh_token));
            yield call(getPromoCode, action);
            history.push("/dashboard");
        } else {
            yield put(Action.loginFailed(true));
        }
    } else {
        yield put(Action.loginFailed(true));
    }
}

function* getPromoCode(action) {
    const state = yield select();
    const { json, response } = yield call(
        apiPost,
        `${config.url.PRIMARY_SERVER}/account/fetchCode`,
        {
            username: action.username,
        },
        ""
    );

    if (json && json.status === 200) {
        yield put(Action.storePromoCode(json.code));
    }
}

function* fetchPaymentInfo(action) {
    const { json, response } = yield call(
        apiPost,
        `${config.url.PRIMARY_SERVER}/stripe/retrieve`,
        {
            email: action.username,
        }
    );

    if (json && json.account_locked) {
        yield put(Action.storePaymentInfo(json.account_locked));
    }
}

function* fetchDisk(action) {
    const state = yield select();
    const { json, response } = yield call(
        apiPost,
        `${config.url.PRIMARY_SERVER}/user/login`,
        {
            username: action.username,
        },
        state.counter.access_token
    );

    if (json && json.status && json.status === 401) {
        yield call(refreshAccess);
        yield call(fetchDisk, action);
    }

    if (json && json.payload && Object.keys(json.payload).length > 0) {
        yield put(
            Action.storeDiskName(
                json.payload[0].disk_name,
                json.payload[0].location
            )
        );
    } else {
        yield put(Action.storeDiskName("", ""));
    }

    yield put(Action.fetchDiskStatus(true));
}

function* loginStudio(action) {
    const { json, response } = yield call(
        apiPost,
        `${config.url.PRIMARY_SERVER}/account/login`,
        {
            username: action.username,
            password: action.password,
        },
        state.counter.access_token
    );

    if (json && json.status && json.status === 401) {
        yield call(refreshAccess);
        yield call(loginStudio, action);
    }

    if (json && json.verified) {
        yield put(Action.storeUsername(action.username));
        yield put(Action.storeIsUser(json.is_user));
        yield put(Action.storeIP(""));
        history.push("/studios");
    } else {
        yield put(Action.loginFailed(true));
    }
}

function* sendFeedback(action) {
    const state = yield select();
    const { json } = yield call(
        apiPost,
        `${config.url.PRIMARY_SERVER}/feedback`,
        {
            username: state.counter.username,
            feedback: action.feedback,
            type: action.feedback_type,
        },
        state.counter.access_token
    );

    console.log(json);

    if (json && json.status && json.status === 401) {
        yield call(refreshAccess);
        yield call(sendFeedback, action);
    }

    yield put(Action.resetFeedback(true));
}

function* pingIPInfo(action) {
    const state = yield select();
    const { json, response } = yield call(
        apiGet,
        "https://ipinfo.io?token=926e38ce447823",
        ""
    );
    yield put(Action.storeIPInfo(json, action.id));
}

function* storeIPInfo(action) {
    const state = yield select();
    const location = `${action.payload.city}, ${action.payload.region}`;

    const { json, response } = yield call(
        apiPost,
        `${config.url.PRIMARY_SERVER}/account/checkComputer`,
        {
            id: action.id,
            username: state.counter.username,
        },
        state.counter.access_token
    );

    if (json && json.status && json.status === 401) {
        yield call(refreshAccess);
        yield call(pingIPInfo, action);
    }

    if (json && json.status === 200) {
        if (json.computers[0].found) {
            yield put(Action.fetchComputers());
        } else {
            const { json1, response1 } = yield call(
                apiPost,
                `${config.url.PRIMARY_SERVER}/account/insertComputer`,
                {
                    id: action.id,
                    username: state.counter.username,
                    location,
                    nickname: json.computers[0].nickname,
                },
                state.counter.access_token
            );
            yield put(Action.fetchComputers());
        }
    }
}

function* fetchComputers(action) {
    const state = yield select();
    const { json, response } = yield call(
        apiPost,
        `${config.url.PRIMARY_SERVER}/account/fetchComputers`,
        {
            username: state.counter.username,
        },
        state.counter.access_token
    );

    if (json && json.status && json.status === 401) {
        yield call(refreshAccess);
        yield call(fetchComputers, action);
    }

    yield put(Action.storeComputers(json.computers));
}

function* attachDisk(action) {
    console.log("ATTACHING DISK");
    const state = yield select();
    const { json, response } = yield call(
        apiPost,
        `${config.url.PRIMARY_SERVER}/disk/attach`,
        {
            disk_name: state.counter.disk,
        },
        state.counter.access_token
    );

    if (json && json.status && json.status === 401) {
        yield call(refreshAccess);
        yield call(attachDisk, action);
    }

    if (response && response.status && response.status === 500) {
        const warning =
            "Unexpectedly lost connection with server. Please close the app and log back in.";
        yield put(Action.changeStatusMessage(warning));
    }

    if (json && json.ID) {
        yield put(Action.fetchVM(json.ID));
    }
}

function formatDate(num) {
    num = String(num);
    const singleDigits = ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9"];
    if (singleDigits.includes(num)) {
        num = `0${num}`;
    }
    return num;
}

function* fetchVM(action) {
    const state = yield select();
    var { json, response } = yield call(
        apiGet,
        `${config.url.PRIMARY_SERVER}/status/`.concat(action.id),
        state.counter.access_token
    );

    while (json.state !== "SUCCESS" && json.state !== "FAILURE") {
        var { json, response } = yield call(
            apiGet,
            `${config.url.PRIMARY_SERVER}/status/`.concat(action.id),
            state.counter.access_token
        );

        if (response && response.status && response.status === 500) {
            const warning =
                "Unexpectedly lost connection with server. Please close the app and log back in.";
            yield put(Action.changeStatusMessage(warning));
        }

        if (json && json.output && json.state === "PENDING") {
            var now = new Date();
            var message = `(${formatDate(now.getHours())}:${formatDate(
                now.getMinutes()
            )}:${formatDate(now.getSeconds())}) ${json.output.msg}`;
            yield put(Action.changeStatusMessage(message));
        }

        yield delay(5000);
    }
    if (json && json.state && json.state === "SUCCESS") {
        if (json.output && json.output.ip) {
            yield put(Action.storeIP(json.output.ip));
        }
    } else {
        var now = new Date();
        var message =
            `(${formatDate(now.getHours())}:${formatDate(
                now.getMinutes()
            )}:${formatDate(now.getSeconds())}) ` +
            `Unexpectedly lost connection with server. Trying again.`;
        yield put(Action.changeStatusMessage(message));
        yield put(Action.attachDisk());
    }
}

function* restartPC(action) {
    const state = yield select();
    const { json, response } = yield call(
        apiPost,
        `${config.url.PRIMARY_SERVER}/vm/restart`,
        {
            username: state.counter.username,
        },
        state.counter.access_token
    );

    if (json && json.status && json.status === 401) {
        yield call(restartPC);
        yield call(restartPC, action);
    }

    if (json && json.ID) {
        yield call(getRestartStatus, json.ID);
    } else {
        yield put(Action.vmRestarted(400));
    }
}

function* getRestartStatus(id) {
    const state = yield select();
    var { json, response } = yield call(
        apiGet,
        `${config.url.PRIMARY_SERVER}/status/`.concat(id),
        state.counter.access_token
    );

    while (json.state === "PENDING" || json.state === "STARTED") {
        var { json, response } = yield call(
            apiGet,
            `${config.url.PRIMARY_SERVER}/status/`.concat(id),
            state.counter.access_token
        );
        yield delay(5000);
    }

    if (json && json.output) {
        yield put(Action.vmRestarted(200));
    }
}

function* sendLogs(action) {
    const state = yield select();
    const { public_ip } = state.counter;
    const { json, response } = yield call(
        apiPost,
        "https://cube-celery-staging.herokuapp.com/logs",
        {
            connection_id: action.connection_id,
            logs: action.logs,
            sender: "client",
            vm_ip: public_ip,
        }
    );
}

export default function* rootSaga() {
    yield all([
        takeEvery(Action.SEND_FEEDBACK, sendFeedback),
        takeEvery(Action.LOGIN_USER, loginUser),
        takeEvery(Action.GOOGLE_LOGIN, googleLogin),
        takeEvery(Action.LOGIN_STUDIO, loginStudio),
        takeEvery(Action.PING_IPINFO, pingIPInfo),
        takeEvery(Action.STORE_IPINFO, storeIPInfo),
        takeEvery(Action.FETCH_COMPUTERS, fetchComputers),
        takeEvery(Action.FETCH_DISK, fetchDisk),
        takeEvery(Action.ATTACH_DISK, attachDisk),
        takeEvery(Action.FETCH_VM, fetchVM),
        takeEvery(Action.RESTART_PC, restartPC),
        takeEvery(Action.SEND_LOGS, sendLogs),
    ]);
}
