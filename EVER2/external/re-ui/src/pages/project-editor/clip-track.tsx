import { Film } from "lucide-react"

import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip"
import { cn } from "@/lib/utils"
import type { ProjectClipData } from "@/components/rockstar-editor/types"
import { clipColorClass, diskPathToFileUrl } from "./utils"

type ClipBlockProps = {
  clip: ProjectClipData
  isSelected: boolean
  totalClips: number
  onSelect: (clipIndex: number) => void
}

const getDisplayName = (clip: ProjectClipData): string =>
  clip.baseName ??
  (clip.path ? clip.path.split(/[\\/]/).pop()?.replace(/\.[^.]+$/, "") ?? null : null) ??
  `Clip ${clip.index + 1}`

export const ClipBlock = ({ clip, isSelected, onSelect }: ClipBlockProps) => {
  const displayName = getDisplayName(clip)
  const previewSrc =
    clip.previewExists && clip.previewDiskPath
      ? diskPathToFileUrl(clip.previewDiskPath)
      : null

  return (
    <Tooltip>
      <TooltipTrigger asChild>
        <button
          onClick={() => onSelect(clip.index)}
          className={cn(
            "group relative flex h-full min-w-[130px] flex-1 flex-col border-r border-white/5 transition-all duration-150 last:border-r-0",
            isSelected ? "bg-amber-400/10" : "bg-transparent hover:bg-white/3",
          )}
        >
          <span
            aria-hidden="true"
            className={cn(
              "absolute left-0 right-0 top-0 h-0.5 transition-opacity duration-150",
              isSelected ? "bg-amber-400/70 opacity-100" : "opacity-0",
            )}
          />

          <div className="relative min-h-0 flex-1 overflow-hidden">
            {previewSrc ? (
              <img
                src={previewSrc}
                alt={displayName}
                className="h-full w-full object-cover"
                draggable={false}
              />
            ) : (
              <div
                className={cn(
                  "flex h-full items-center justify-center border",
                  clipColorClass(clip.index),
                )}
              >
                <Film
                  className={cn(
                    "size-4 transition-colors",
                    isSelected ? "text-amber-400/50" : "text-white/18 group-hover:text-white/30",
                  )}
                />
              </div>
            )}
          </div>

          <div
            className={cn(
              "shrink-0 border-t border-white/5 px-2 py-1.5 transition-colors duration-150",
              isSelected ? "bg-amber-400/8" : "bg-white/1",
            )}
          >
            <p
              className={cn(
                "truncate text-[10px] font-medium leading-none",
                isSelected
                  ? "text-amber-400/80"
                  : "text-white/35 group-hover:text-white/55",
              )}
            >
              {displayName}
            </p>
          </div>
        </button>
      </TooltipTrigger>
      <TooltipContent
        side="top"
        className="max-w-72 border-white/10 bg-[#0c1016] text-white/80"
      >
        <p className="text-[11px] font-semibold">{displayName}</p>
        {clip.path && (
          <p className="mt-0.5 break-all text-[10px] text-white/40">{clip.path}</p>
        )}
        {clip.uid != null && (
          <p className="mt-0.5 text-[10px] text-white/35">UID: {clip.uid}</p>
        )}
        <p className="mt-0.5 text-[10px] text-white/28">
          Index: {clip.index}
          {clip.exists === false && " · File missing from disk"}
          {clip.previewExists === false && " · No thumbnail"}
        </p>
      </TooltipContent>
    </Tooltip>
  )
}

export const SkeletonClipBlock = ({ index }: { index: number }) => (
  <div className="relative flex h-full min-w-[130px] flex-1 flex-col border-r border-white/5 last:border-r-0">
    <div className="flex-1 animate-pulse bg-white/4" />
    <div className="shrink-0 border-t border-white/5 bg-white/1 px-2 py-1.5">
      <div
        className="h-2 animate-pulse rounded bg-white/8"
        style={{ width: `${40 + (index % 3) * 20}%` }}
      />
    </div>
  </div>
)


type ClipTrackProps = {
  clip: ProjectClipData
  left: number
  width: number
  isOddRow: boolean
  isActive?: boolean
}

const LABEL_WIDTH = 110

export { LABEL_WIDTH }

export const ClipTrack = ({
  clip,
  left,
  width,
  isOddRow,
  isActive = false,
}: ClipTrackProps) => {
  const displayName =
    clip.baseName ?? (clip.path ? clip.path.split(/[\\/]/).pop() : null) ?? `Clip ${clip.index + 1}`

  return (
    <div
      className={cn(
        "group flex h-9 shrink-0 items-center border-b border-white/4",
        isOddRow ? "bg-white/1.5" : "bg-transparent",
      )}
    >
      <div
        className="flex h-full shrink-0 items-center border-r border-white/5 px-3"
        style={{ width: LABEL_WIDTH }}
      >
        <Film
          className={cn(
            "mr-2 size-3 shrink-0",
            isActive ? "text-amber-400/70" : "text-white/20",
          )}
        />
        <span
          className={cn(
            "truncate text-[10px] font-medium",
            isActive ? "text-white/70" : "text-white/35",
          )}
          title={clip.path ?? displayName}
        >
          {displayName}
        </span>
      </div>

      <div className="relative flex-1">
        <Tooltip>
          <TooltipTrigger asChild>
            <div
              className={cn(
                "absolute inset-y-1 cursor-pointer rounded-md border transition-all duration-150",
                clipColorClass(clip.index),
                "hover:brightness-125",
                isActive && "ring-1 ring-amber-400/40",
              )}
              style={{ left, width: Math.max(width, 6) }}
            />
          </TooltipTrigger>
          <TooltipContent
            side="top"
            className="max-w-72 border-white/10 bg-[#0c1016] text-white/80"
          >
            <p className="text-[11px] font-semibold">{displayName}</p>
            {clip.path && (
              <p className="mt-0.5 break-all text-[10px] text-white/40">
                {clip.path}
              </p>
            )}
            {clip.uid != null && (
              <p className="mt-0.5 text-[10px] text-white/35">
                UID: {clip.uid}
              </p>
            )}
            <p className="mt-0.5 text-[10px] text-white/28">
              Index: {clip.index}
              {clip.exists === false && " · Missing from disk"}
            </p>
          </TooltipContent>
        </Tooltip>
      </div>
    </div>
  )
}

export const SkeletonClipTrack = ({
  index,
  widthPercent,
}: {
  index: number
  widthPercent: number
}) => {
  return (
    <div
      className={cn(
        "flex h-9 shrink-0 items-center border-b border-white/4",
        index % 2 === 1 ? "bg-white/1.5" : "bg-transparent",
      )}
      style={{ minWidth: "100%" }}
    >
      <div
        className="flex h-full shrink-0 items-center border-r border-white/5 px-3"
        style={{ width: LABEL_WIDTH }}
      >
        <div className="h-2.5 w-16 animate-pulse rounded bg-white/8" />
      </div>

      <div className="relative flex-1 py-1">
        <div
          className="absolute inset-y-1 animate-pulse rounded-md bg-amber-400/8"
          style={{ left: 8, width: `${widthPercent}%` }}
        />
      </div>
    </div>
  )
}
