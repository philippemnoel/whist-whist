import {
  userEmail,
  userAccessToken,
  userConfigToken
} from '@app/main/observables/user'
import {
  containerAssignPolling,
  containerAssignFailure
} from '@app/main/observables/container'
import { loadingFrom, pollMap } from '@app/utils/observables'
import {
  hostServiceInfo,
  hostServiceInfoValid,
  hostServiceInfoIP,
  hostServiceInfoPort,
  hostServiceInfoPending,
  hostServiceInfoSecret,
  hostServiceConfig,
  hostServiceConfigValid,
  hostServiceConfigError
} from '@app/utils/host'
import { debug, error } from '@app/utils/logging'
import { from, merge } from 'rxjs'
import {
  map,
  take,
  share,
  filter,
  skipWhile,
  takeLast,
  takeWhile,
  takeUntil,
  exhaustMap,
  withLatestFrom
} from 'rxjs/operators'
import { pick } from 'lodash'

export const hostInfoRequest = containerAssignPolling.pipe(
  skipWhile((res) => res?.json.state !== 'PENDING'),
  take(1),
  withLatestFrom(userEmail, userAccessToken),
  map(([_, email, token]) => [email, token]),
  share()
)

export const hostInfoPolling = hostInfoRequest.pipe(
  pollMap(1000, async ([email, token]) => await hostServiceInfo(email, token)),
  takeWhile((res) => hostServiceInfoPending(res), true),
  takeUntil(containerAssignFailure),
  share()
)

hostInfoPolling.subscribe((res) =>
  console.log('host poll', res?.status, res?.json)
)

export const hostInfoSuccess = hostInfoPolling.pipe(
  takeLast(1),
  filter((res) => hostServiceInfoValid(res))
)

export const hostInfoFailure = hostInfoPolling.pipe(
  takeLast(1),
  filter((res) => hostServiceInfoPending(res) || !hostServiceInfoValid(res))
)

export const hostInfoLoading = loadingFrom(
  hostInfoRequest,
  hostInfoSuccess,
  hostInfoFailure
)

export const hostConfigRequest = hostInfoSuccess.pipe(
  map((res) => [
    hostServiceInfoIP(res),
    hostServiceInfoPort(res),
    hostServiceInfoSecret(res)
  ]),
  withLatestFrom(userEmail, userConfigToken),
  map(([[ip, port, secret], email, token]) => [
    ip,
    port,
    secret,
    email,
    token
  ])
)

export const hostConfigProcess = hostConfigRequest.pipe(
  exhaustMap(([ip, port, secret, email, token]) =>
    from(hostServiceConfig(ip, port, secret, email, token))
  ),
  share()
)

export const hostConfigSuccess = hostConfigProcess.pipe(
  filter((res) => hostServiceConfigValid(res))
)

export const hostConfigFailure = hostConfigProcess.pipe(
  filter((res) => hostServiceConfigError(res))
)

export const hostConfigLoading = loadingFrom(
  hostInfoRequest,
  hostInfoSuccess,
  hostInfoFailure
)

// Logging
merge(
  hostInfoRequest.pipe(debug('hostInfoRequest')),
  hostInfoSuccess.pipe(debug('hostInfoSuccess')),
  hostInfoFailure.pipe(error('hostInfoFailure')),
  hostConfigRequest.pipe(debug('hostConfigRequest', 'value:', () => 'hostConfigRequest emitted')),
  hostConfigProcess.pipe(debug('hostConfigRequest', 'printing only status, json:', (obj) => pick(obj, ['status', 'json']))),
  hostConfigSuccess.pipe(debug('hostConfigSuccess', 'printing only status, json:', (obj) => pick(obj, ['status', 'json']))),
  hostConfigFailure.pipe(error('hostConfigFailure', 'error:'))
).subscribe()
