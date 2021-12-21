import { execCommandByOS } from "@app/utils/execCommand"
import {
  INITIAL_KEY_REPEAT_MIN_LINUX,
  INITIAL_KEY_REPEAT_MIN_MAC,
  INITIAL_KEY_REPEAT_RANGE_LINUX,
  INITIAL_KEY_REPEAT_RANGE_MAC,
  KEY_REPEAT_MIN_MAC,
  KEY_REPEAT_RANGE_LINUX,
  KEY_REPEAT_RANGE_MAC,
  KEY_REPEAT_MIN_LINUX,
} from "@app/constants/keyboard"

const getInitialKeyRepeat = () => {
  const initialKeyRepeatRaw = execCommandByOS(
    "defaults read NSGlobalDomain InitialKeyRepeat",
    /* eslint-disable no-template-curly-in-string */
    "xset -q | grep 'auto repeat delay'",
    "",
    ".",
    {},
    "pipe"
  )

  let initialKeyRepeat =
    initialKeyRepeatRaw?.toString()?.replace(/\n$/, "") ?? ""

  // Extract value from bash output
  if (process.platform === "linux" && initialKeyRepeat !== "") {
    const startIndex =
      initialKeyRepeat.indexOf("auto repeat delay:") +
      "auto repeat delay:".length +
      2
    const endIndex = initialKeyRepeat.indexOf("repeat rate:") - 4
    initialKeyRepeat = initialKeyRepeat.substring(startIndex, endIndex)
  } else if (process.platform === "darwin" && initialKeyRepeat !== "") {
    // Convert the key repetition delay from Mac scale (shortest=15, longest=120) to Linux scale (shortest=115, longest=2000)
    const initialKeyRepeatFloat =
      ((parseInt(initialKeyRepeat) - INITIAL_KEY_REPEAT_MIN_MAC) /
        INITIAL_KEY_REPEAT_RANGE_MAC) *
        INITIAL_KEY_REPEAT_RANGE_LINUX +
      INITIAL_KEY_REPEAT_MIN_LINUX
    initialKeyRepeat = initialKeyRepeatFloat.toFixed()
  }

  return initialKeyRepeat
}

const getKeyRepeat = () => {
  const keyRepeatRaw = execCommandByOS(
    "defaults read NSGlobalDomain KeyRepeat",
    /* eslint-disable no-template-curly-in-string */
    "xset -q | grep 'auto repeat delay'",
    "",
    ".",
    {},
    "pipe"
  )

  let keyRepeat = keyRepeatRaw !== null ? keyRepeatRaw.toString() : ""

  // Remove trailing '\n'
  keyRepeat.replace(/\n$/, "")
  // Extract value from bash output
  if (process.platform === "linux" && keyRepeat !== "") {
    const startIndex =
      keyRepeat.indexOf("repeat rate:") + "repeat rate:".length + 2
    const endIndex = keyRepeat.length
    keyRepeat = keyRepeat.substring(startIndex, endIndex)
  } else if (process.platform === "darwin" && keyRepeat !== "") {
    // Convert the key repetition delay from Mac scale (slowest=120, fastest=2) to Linux scale (slowest=9, fastest=1000). NB: the units on Mac and Linux are multiplicative inverse.
    const keyRepeatFloat: number =
      (1.0 -
        (parseInt(keyRepeat) - KEY_REPEAT_MIN_MAC) / KEY_REPEAT_RANGE_MAC) *
        KEY_REPEAT_RANGE_LINUX +
      KEY_REPEAT_MIN_LINUX
    keyRepeat = keyRepeatFloat.toFixed()
  }

  return keyRepeat
}

export { getInitialKeyRepeat, getKeyRepeat }
