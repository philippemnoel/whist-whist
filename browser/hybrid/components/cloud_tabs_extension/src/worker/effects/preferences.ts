import { withLatestFrom } from "rxjs/operators"
import { Socket } from "socket.io-client"

import { darkModeChanged } from "@app/worker/events/preferences"
import { socket, socketConnected } from "@app/worker/events/socketio"

import { whistState } from "@app/worker/utils/state"

socketConnected.subscribe(async (socket: Socket) => {
    // Set keyboard repeat rate
    const delay = new Promise((resolve) => {
        ;(chrome as any).whist.getKeyboardRepeatInitialDelay((delay: number) => {
            resolve(delay)
        })
    })
    const rate = new Promise((resolve) => {
        ;(chrome as any).whist.getKeyboardRepeatRate((rate: number) => {
            resolve(rate)
        })
    })
    socket.emit("keyboard-repeat-rate-changed", await delay, await rate)

    // Set timezone
    const timezone = Intl.DateTimeFormat().resolvedOptions().timeZone
    socket.emit("timezone-changed", timezone)

    // Set dark mode
    const usingDarkMode = new Promise((resolve) => {
        ;(chrome as any).braveTheme.getBraveThemeType((themeType: string) => {
          resolve(themeType === "Dark")
        })
    })

    socket.emit("init-preferences", await usingDarkMode)
})

darkModeChanged
    .pipe(withLatestFrom(socket))
    .subscribe(([isDarkMode, socket]: [boolean, Socket]) => {
        socket.emit("darkmode-changed", isDarkMode)
    })

// TODO: detect changes in keyboard repeat rate, keyboard repeat delay, and timezone
