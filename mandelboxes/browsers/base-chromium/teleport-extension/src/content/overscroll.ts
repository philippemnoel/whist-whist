const damping = 0.8
const maxOffset = 100

const content =
  document.compatMode === "BackCompat"
    ? document.body
    : document.documentElement

// states
let offset = 0
let rendered = 0
let lastDis = 0
let backFlag = false

let timer: any

// Wheel delta normalizing
// copied from smooth-scrollbar/src/utils/get-delta.js
const DELTA_SCALE = {
  STANDARD: 1,
  OTHERS: -3,
}

const DELTA_MODE = [1.0, 28.0, 500.0]

const resetFlag = () => {
  clearTimeout(timer)
  timer = setTimeout(() => {
    backFlag = false
  }, 250)
}

const trim = (update: number) => {
  let abs = Math.abs(update)
  if (abs > 300) abs = 300

  return update >= 0 ? abs : -1 * abs
}

const render = () => {
  if (!offset && !rendered) {
    lastDis = 0

    return requestAnimationFrame(render)
  }

  const dis = offset - rendered

  if (lastDis * dis <= 0) {
    backFlag = true
  }

  lastDis = dis

  const next = offset - ((dis * damping) | 0)

  content.style.transform = `translate3d(0, ${-next}px, 0)`

  rendered = next
  offset = (offset * damping) | 0

  requestAnimationFrame(render)
}

const getDeltaMode = (mode: any) => DELTA_MODE[mode] || DELTA_MODE[0]

const getDelta = (evt: WheelEvent) => {
  const mode = getDeltaMode(evt.deltaMode)

  return {
    x: (evt.deltaX / DELTA_SCALE.STANDARD) * mode,
    y: (evt.deltaY / DELTA_SCALE.STANDARD) * mode,
  }
}

const isOnTopEdge = (delta: number) => {
  const { scrollTop } = content
  return scrollTop === 0 && delta <= 0
}

const isOnBottomEdge = (delta: number) => {
  const { scrollTop, scrollHeight, clientHeight } = content
  const max = scrollHeight - clientHeight

  return scrollTop === max && delta >= 0
}

const handler = (evt: WheelEvent) => {
  // Don't overscroll if the page has no scrollbar
  const canScroll = content.scrollHeight > content.clientHeight
  if (!canScroll) return

  const { y } = getDelta(evt)

  // Check if the user has scrolled to the top/bottom limits of the page
  const onTopEdge = isOnTopEdge(y)
  const onBottomEdge = isOnBottomEdge(y)

  if (!onTopEdge && !onBottomEdge) {
    content.style.transform = "none"
    return
  }

  resetFlag()
  evt.preventDefault()

  // If the user is overscrolling, play the animation
  if (!backFlag && y) {
    const update = (y * (maxOffset - Math.abs(offset))) / maxOffset

    if (Math.abs(update) < 10) return

    const updated = trim(offset + update)
    if ((onTopEdge && updated > 0) || (onBottomEdge && updated < 0)) {
      content.style.transform = "none"
      offset = 0
    } else {
      offset = updated
    }
  }
}

render()

const initYOverscrollHandler = () => {
  window.addEventListener("wheel", handler, { passive: false })
}

export { initYOverscrollHandler }
