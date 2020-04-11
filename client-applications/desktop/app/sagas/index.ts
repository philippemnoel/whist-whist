import { put, take, takeLatest, takeEvery, all, call, select, delay } from 'redux-saga/effects';
import { channel } from 'redux-saga';
import { apiPost, apiGet } from '../utils/api.ts';
import * as Action from '../actions/counter';
import { configureStore, history } from '../store/configureStore';

import { config } from '../constants/config.ts'


function* loginUser(action) {
  const {json, response} = yield call(apiPost, config.url.PRIMARY_SERVER + '/account/login', {
    username: action.username,
    password: action.password
  })

  if(json && json.verified) {
    yield put(Action.fetchDisk(action.username))

    var email = action.username
    yield put(Action.storeUsername(action.username))
    yield put(Action.storeIsUser(json.is_user))
    history.push("/counter");
  } else {
    yield put(Action.loginFailed(true));
  }
}

function* fetchDisk(action) {
  const state = yield select()
  const {json, response} = yield call(apiPost, config.url.PRIMARY_SERVER + '/user/login', {
    username: action.username
  })

  if(json && json.payload && Object.keys(json.payload).length > 0) {
    yield put(Action.storeDiskName(json.payload[0].disk_name))
  } else {
    yield put(Action.storeDiskName(''))
  }

  yield put(Action.fetchDiskStatus(true))
}

function* loginStudio(action) {
  const {json, response} = yield call(apiPost, config.url.PRIMARY_SERVER + '/account/login', {
    username: action.username,
    password: action.password
  })

  if(json && json.verified) {
    yield put(Action.storeUsername(action.username))
    yield put(Action.storeIsUser(json.is_user))
    yield put(Action.storeIP(''))
    history.push("/studios");
  } else {
    yield put(Action.loginFailed(true));
  }
}

function* trackUserActivity(action) {
  const state = yield select()
  if(action.logon) {
    const {json, response} = yield call(apiPost, config.url.PRIMARY_SERVER + '/tracker/logon', {
      username: state.counter.username,
      is_user: state.counter.isUser
    })
  } else {
    const {json, response} = yield call(apiPost, config.url.PRIMARY_SERVER + '/tracker/logoff', {
      username: state.counter.username,
      is_user: state.counter.isUser
    })
  }
}

function* sendFeedback(action) {
  const state = yield select()
  const {json, response} = yield call(apiPost, config.url.MAIL_SERVER + '/feedback', {
    username: state.counter.username,
    feedback: action.feedback
  })
  yield put(Action.resetFeedback(true))
}

function* pingIPInfo(action) {
  const state = yield select()
  const {json, response} = yield call(apiGet, 'https://ipinfo.io?token=926e38ce447823')
  yield put(Action.storeIPInfo(json, action.id))
}

function* storeIPInfo(action) {
  const state = yield select()
  var location = action.payload.city + ", " + action.payload.region

  const {json, response} = yield call(apiPost, config.url.PRIMARY_SERVER + '/account/checkComputer', {
    id: action.id,
    username: state.counter.username
  })


  if(json && json.status === 200) {
    if(json.computers[0].found) {
      console.log("ID FOUND")
      console.log(json)
      yield put(Action.fetchComputers())
    } else {
      console.log("ID NOT FOUND")
      console.log(json)
      const {json1, response1} = yield call(apiPost, config.url.PRIMARY_SERVER + '/account/insertComputer', {
        id: action.id,
        username: state.counter.username,
        location: location,
        nickname: json.computers[0].nickname
      })
      yield put(Action.fetchComputers())
    }
  }
}

function* fetchComputers(action) {
  const state = yield select()
  const {json, response} = yield call(apiPost, config.url.PRIMARY_SERVER + '/account/fetchComputers', {
    username: state.counter.username
  })
  yield put(Action.storeComputers(json.computers))
}

function* attachDisk(action) {
  const state = yield select()
  const {json, response} = yield call(apiPost, config.url.PRIMARY_SERVER + '/disk/attach', {
    disk_name: state.counter.disk
  })
  console.log("INITIAL DISK ATTACH CALL MADE")
  console.log(json)
  if(json && json.ID) {
    yield put(Action.fetchVM(json.ID))
  }
}

function* fetchVM(action) {
  var { json, response } = yield call(apiGet, (config.url.PRIMARY_SERVER + '/status/').concat(action.id))
  console.log(json)
  while (json.state === "PENDING" || json.state === "STARTED") {
    console.log("PENDING")
    var { json, response } = yield call(apiGet, (config.url.PRIMARY_SERVER + '/status/').concat(action.id))
    yield delay(5000)
  }
  console.log("DISK ATTACHED")
  console.log(json)
  if(json && json.output && json.output.public_ip) {
    yield put(Action.storeIP(json.output.public_ip))
  }
}


export default function* rootSaga() {
 	yield all([
     takeEvery(Action.TRACK_USER_ACTIVITY, trackUserActivity),
     takeEvery(Action.LOGIN_USER, loginUser),
     takeEvery(Action.SEND_FEEDBACK, sendFeedback),
     takeEvery(Action.LOGIN_STUDIO, loginStudio),
     takeEvery(Action.PING_IPINFO, pingIPInfo),
     takeEvery(Action.STORE_IPINFO, storeIPInfo),
     takeEvery(Action.FETCH_COMPUTERS, fetchComputers),
     takeEvery(Action.FETCH_DISK, fetchDisk),
     takeEvery(Action.ATTACH_DISK, attachDisk),
     takeEvery(Action.FETCH_VM, fetchVM)
  ])
}
