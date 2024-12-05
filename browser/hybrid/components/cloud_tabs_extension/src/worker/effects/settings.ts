import { initSettingsSent } from "@app/worker/events/settings"
import { whistState } from "@app/worker/utils/state"

initSettingsSent
  .subscribe((_: any) => {
    whistState.sentInitSettings = true
  })
