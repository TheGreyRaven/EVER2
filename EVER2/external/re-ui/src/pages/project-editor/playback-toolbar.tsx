import { Pause, Play, SkipBack, SkipForward, Square } from "lucide-react"

import { Button } from "@/components/ui/button"
import { cn } from "@/lib/utils"
import { formatDuration } from "./utils"

type PlaybackState = "idle" | "playing" | "paused"

type PlaybackToolbarProps = {
  totalDurationMs: number
  playheadMs: number
  playbackState: PlaybackState
  onPlay: () => void
  onPause: () => void
  onStop: () => void
  onSeekStart: () => void
  onSeekEnd: () => void
}

export const PlaybackToolbar = ({
  totalDurationMs,
  playheadMs,
  playbackState,
  onPlay,
  onPause,
  onStop,
  onSeekStart,
  onSeekEnd,
}: PlaybackToolbarProps) => {
  const isPlaying = playbackState === "playing"
  const isPaused = playbackState === "paused"

  return (
    <div className="flex h-11 shrink-0 items-center gap-1 border-b border-white/5 bg-white/1 px-4">
      <div className="flex items-center gap-0.5">
        <Button
          variant="ghost"
          size="icon-xs"
          onClick={onSeekStart}
          title="Go to start"
          className="text-white/30 hover:text-white/70"
          disabled={playheadMs === 0}
        >
          <SkipBack className="size-3.5" />
        </Button>

        {isPlaying ? (
          <Button
            variant="ghost"
            size="icon-xs"
            onClick={onPause}
            title="Pause"
            className="text-amber-400/70 hover:text-amber-400"
          >
            <Pause className="size-3.5" />
          </Button>
        ) : (
          <Button
            variant="ghost"
            size="icon-xs"
            onClick={onPlay}
            title={isPaused ? "Resume" : "Play"}
            className={cn(
              "hover:text-amber-400",
              isPaused ? "text-amber-400/60" : "text-white/40",
            )}
          >
            <Play className="size-3.5" />
          </Button>
        )}

        <Button
          variant="ghost"
          size="icon-xs"
          onClick={onStop}
          title="Stop"
          className="text-white/30 hover:text-white/70"
          disabled={playbackState === "idle"}
        >
          <Square className="size-3" />
        </Button>

        <Button
          variant="ghost"
          size="icon-xs"
          onClick={onSeekEnd}
          title="Go to end"
          className="text-white/30 hover:text-white/70"
          disabled={playheadMs >= totalDurationMs}
        >
          <SkipForward className="size-3.5" />
        </Button>
      </div>

      <div className="mx-2 h-4 w-px bg-white/8" />

      <div className="flex items-center gap-1 font-mono">
        <span className="text-[12px] font-semibold text-white/70 tabular-nums">
          {formatDuration(playheadMs)}
        </span>
        <span className="text-[11px] text-white/20">/</span>
        <span className="text-[11px] text-white/32 tabular-nums">
          {formatDuration(totalDurationMs)}
        </span>
      </div>

      <div className="ml-auto">
        {isPlaying && (
          <div className="flex items-center gap-1.5">
            <span className="size-1.5 animate-pulse rounded-full bg-amber-400" />
            <span className="text-[9.5px] font-semibold uppercase tracking-[0.18em] text-amber-400/60">
              Playing
            </span>
          </div>
        )}
        {isPaused && (
          <span className="text-[9.5px] font-semibold uppercase tracking-[0.18em] text-white/28">
            Paused
          </span>
        )}
      </div>
    </div>
  )
}
