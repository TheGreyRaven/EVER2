import { useEffect, useState } from "react"

// Scale entire UI so its the same across all resolutions
const BASE_WIDTH = 1920
const BASE_HEIGHT = 1080

const getViewportSize = () => {
  const doc = document.documentElement
  const body = document.body

  const width = Math.max(
    window.innerWidth || 0,
    doc?.clientWidth || 0,
    body?.clientWidth || 0
  )

  const height = Math.max(
    window.innerHeight || 0,
    doc?.clientHeight || 0,
    body?.clientHeight || 0
  )

  return { width, height }
}

const getScale = (): number => {
  const { width, height } = getViewportSize()

  // CEF iframes can briefly report 0x0 during startup; avoid scaling the app to 0.
  if (width <= 0 || height <= 0) {
    return 1
  }

  const scale = Math.min(width / BASE_WIDTH, height / BASE_HEIGHT)
  return Number.isFinite(scale) && scale > 0 ? scale : 1
}

export const useViewportScale = (): number => {
  const [scale, setScale] = useState(1)

  useEffect(() => {
    const handleResize = () => setScale(getScale())

    handleResize()

    // Some CEF startup paths never emit an early resize event after iframe layout.
    let frame = 0
    let rafId = 0
    const tick = () => {
      frame += 1
      setScale(getScale())
      if (frame < 12) {
        rafId = window.requestAnimationFrame(tick)
      }
    }
    rafId = window.requestAnimationFrame(tick)

    window.addEventListener("resize", handleResize)
    return () => {
      window.cancelAnimationFrame(rafId)
      window.removeEventListener("resize", handleResize)
    }
  }, [])

  return scale
}
