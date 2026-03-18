export const diskPathToFileUrl = (diskPath: string): string => {
  if (!diskPath) return ""

  const withSlashes = diskPath.replace(/\\/g, "/")

  const encoded = withSlashes
    .split("/")
    .map((segment, index) => {
      // Keep the drive letter as-is (e.g. "C:")
      if (index === 0 && /^[A-Za-z]:$/.test(segment)) return segment
      return encodeURIComponent(segment)
    })
    .join("/")
  return `https://ever2-asset/${encoded}`
}

export const formatDuration = (ms: number): string => {
  const totalSecs = Math.max(0, Math.floor(ms / 1000))
  const hours = Math.floor(totalSecs / 3600)
  const mins = Math.floor((totalSecs % 3600) / 60)
  const secs = totalSecs % 60

  if (hours > 0) {
    return `${hours}:${String(mins).padStart(2, "0")}:${String(secs).padStart(2, "0")}`
  }
  return `${mins}:${String(secs).padStart(2, "0")}`
}

export const getTickInterval = (durationMs: number): number => {
  const secs = durationMs / 1000
  if (secs <= 30) return 2_000
  if (secs <= 120) return 5_000
  if (secs <= 600) return 30_000
  if (secs <= 3600) return 60_000
  return 300_000
}

export const clipColorClass = (index: number): string => {
  const classes = [
    "bg-amber-400/25 border-amber-400/35",
    "bg-amber-300/20 border-amber-300/30",
    "bg-yellow-400/22 border-yellow-400/32",
    "bg-orange-400/22 border-orange-400/32",
    "bg-amber-500/20 border-amber-500/30",
  ]
  return classes[index % classes.length]
}
