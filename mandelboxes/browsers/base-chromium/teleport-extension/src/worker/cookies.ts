import { Socket } from "socket.io-client"

const initAddCookieListener = (socket: Socket) => {
  socket.on("add-cookie", (cookies: any[]) => {
    let cookie = cookies[0]
    const url = cookie.domain.startsWith(".")
      ? `https://${cookie.domain.slice(1)}${cookie.path}`
      : `https://${cookie.domain}${cookie.path}`

    // See https://developer.chrome.com/docs/extensions/reference/cookies/#method-set
    // for why we construct the cookie details this way
    const details = {
        ...!cookie.hostOnly && {domain: cookie.domain},
        ...!cookie.session && {expirationDate: cookie.expirationDate},
        httpOnly: cookie.httpOnly,
        name: cookie.name,
        path: cookie.path,
        sameSite: cookie.sameSite,
        secure: cookie.secure,
        storeId: cookie.storeId,
        value: cookie.value,
        url,
    } as chrome.cookies.SetDetails

    chrome.cookies.set(details)
  })
}

const initRemoveCookieListener = (socket: Socket) => {
  socket.on("remove-cookie", (cookies: any[]) => {
    const cookie = cookies[0]
    const url = cookie.domain.startsWith(".")
      ? `https://${cookie.domain.slice(1)}`
      : `https://${cookie.domain}`
    chrome.cookies.remove({
      name: cookie.name,
      url,
    })
  })
}

export { initAddCookieListener, initRemoveCookieListener }
