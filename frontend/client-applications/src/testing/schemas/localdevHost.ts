/*
 * Mocks the mandelboxCreateFlow to hardcode connecting to a specific
 * host service. This is useful when modifying the API between the
 * client app and the host service.
 *
 * Usage:
 *   HOST_IP=[ip address] yarn test:manual localdevHost
 *
 * Note:
 *   If you want to actually work with the mandelbox, make sure to build
 *   it beforehand. If built using `build_local_mandelbox.sh`, make sure
 *   to call `run_local_mandelbox.sh` before attempting to run the above,
 *   as this will create a new mandelbox with the correct protocol build
 *   instead of an outdated one.
 */
import { mapTo, delay } from "rxjs/operators"
import { MockSchema } from "@app/@types/schema"
import crypto from "crypto"

const localdevHost: MockSchema = {
  mandelboxCreateFlow: (trigger) => {
    return {
      success: trigger.pipe(
        delay(2000),
        mapTo({
          mandelboxID: crypto.randomUUID(),
          ip: process.env.HOST_IP,
        })
      ),
    }
  },
}

export default localdevHost
