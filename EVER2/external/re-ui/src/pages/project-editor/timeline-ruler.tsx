import { formatDuration, getTickInterval } from "./utils"

type TimelineRulerProps = {
  durationMs: number
  timelineWidth: number
  playheadMs: number
}

export const TimelineRuler = ({
  durationMs,
  timelineWidth,
  playheadMs,
}: TimelineRulerProps) => {
  const tickInterval = getTickInterval(durationMs)
  const totalSecs = durationMs / 1000
  const tickCount = Math.floor(totalSecs / (tickInterval / 1000)) + 1
  const pxPerMs = durationMs > 0 ? timelineWidth / durationMs : 0

  return (
    <div
      className="relative flex h-8 shrink-0 select-none items-end border-b border-white/6 bg-white/2"
      style={{ width: timelineWidth }}
    >
      {Array.from({ length: tickCount }, (_, i) => {
        const tickMs = i * tickInterval
        const x = Math.round(tickMs * pxPerMs)
        const label = formatDuration(tickMs)
        return (
          <div
            key={tickMs}
            className="absolute bottom-0 flex flex-col items-start"
            style={{ left: x }}
          >
            <span className="mb-1 pl-1 text-[9px] font-medium tabular-nums text-white/25">
              {label}
            </span>
            <div className="h-2 w-px bg-white/15" />
          </div>
        )
      })}

      {Array.from({ length: tickCount }, (_, i) => {
        const subMs = i * tickInterval + tickInterval / 2
        if (subMs >= durationMs) return null
        const x = Math.round(subMs * pxPerMs)
        return (
          <div
            key={`sub-${subMs}`}
            className="absolute bottom-0"
            style={{ left: x }}
          >
            <div className="h-1.5 w-px bg-white/8" />
          </div>
        )
      })}

      {playheadMs > 0 && playheadMs <= durationMs && (
        <div
          className="absolute bottom-0 z-10"
          style={{ left: Math.round(playheadMs * pxPerMs) }}
        >
          <div className="h-8 w-px bg-amber-400/70" />
        </div>
      )}
    </div>
  )
}
