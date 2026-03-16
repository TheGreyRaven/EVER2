import { useEffect, useState } from "react"

// Scale entire UI so its the same across all resolutions
const BASE_WIDTH = 1920
const BASE_HEIGHT = 1080

const getScale = (): number =>
  Math.min(window.innerWidth / BASE_WIDTH, window.innerHeight / BASE_HEIGHT)

export const useViewportScale = (): number => {
  const [scale, setScale] = useState(getScale)

  useEffect(() => {
    const handleResize = () => setScale(getScale())
    window.addEventListener("resize", handleResize)
    return () => window.removeEventListener("resize", handleResize)
  }, [])

  return scale
}
