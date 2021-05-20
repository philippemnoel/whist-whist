import { mapTo, tap, delay } from "rxjs/operators"
import { MockSchema } from "@app/@types/schema"

const protocolLaunchError: MockSchema = {
  protocolLaunchFlow: (trigger) => ({
    failure: trigger.pipe(
      delay(2000),
      mapTo({ status: 400, json: { access_token: "" } }),
      tap(() => console.log("MOCKED FAILURE"))
    ),
  }),
}

export default protocolLaunchError
