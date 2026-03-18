import { useEffect, useState } from "react"
import { Film, MousePointer2 } from "lucide-react"

import { useProjectStore } from "@/store/project-store"
import { ClipBlock, SkeletonClipBlock } from "./clip-track"
import { diskPathToFileUrl } from "./utils"

const SKELETON_COUNT = 5

const getClipDisplayName = (clip: { index: number; baseName?: string; path?: string }): string =>
  clip.baseName ??
  (clip.path ? clip.path.split(/[\\/]/).pop()?.replace(/\.[^.]+$/, "") ?? null : null) ??
  `Clip ${clip.index + 1}`

export const TimelinePanel = () => {
  const selectedProject = useProjectStore((s) => s.selectedProject)
  const isLoading = useProjectStore((s) => s.isLoading)
  const payload = useProjectStore((s) => s.payload)

  const [selectedClipIndex, setSelectedClipIndex] = useState(0)

  const clips = selectedProject?.clips ?? []

  // Auto-select the first clip whenever the active project changes
  useEffect(() => {
    setSelectedClipIndex(0)
  }, [selectedProject?.index])

  const selectedClip =
    clips.find((c) => c.index === selectedClipIndex) ?? clips[0] ?? null

  const showSkeleton = isLoading || payload == null
  const showPlaceholder = !selectedProject && !isLoading && payload != null
  const showContent = !!selectedProject && !showSkeleton

  const previewSrc =
    selectedClip?.previewExists && selectedClip?.previewDiskPath
      ? diskPathToFileUrl(selectedClip.previewDiskPath)
      : null

  const displayName = selectedClip ? getClipDisplayName(selectedClip) : null

  return (
    <div className="flex h-full flex-col overflow-hidden">

      {showSkeleton && (
        <>
          <div className="flex h-10 shrink-0 items-center gap-3 border-b border-white/5 bg-white/1 px-4">
            <div className="h-2.5 w-28 animate-pulse rounded bg-white/8" />
            <div className="ml-auto h-2 w-10 animate-pulse rounded bg-white/5" />
          </div>
          <div className="min-h-0 flex-1 animate-pulse bg-white/2" />
          <div className="flex h-24 shrink-0 overflow-hidden border-t border-white/5">
            {Array.from({ length: SKELETON_COUNT }, (_, i) => (
              <SkeletonClipBlock key={i} index={i} />
            ))}
          </div>
        </>
      )}

      {showPlaceholder && (
        <div className="flex flex-1 flex-col items-center justify-center gap-3 p-8 text-center">
          <div className="flex size-12 items-center justify-center rounded-xl border border-white/6 bg-white/3">
            <MousePointer2 className="size-5 text-white/20" />
          </div>
          <p className="text-[11px] leading-relaxed text-white/28">
            Select a project from the left panel to view its timeline.
          </p>
        </div>
      )}

      {showContent && (
        <>
          <div className="flex h-10 shrink-0 items-center gap-3 border-b border-white/5 bg-white/1 px-4">
            {displayName ? (
              <p className="truncate text-[12px] font-semibold text-white/70">
                {displayName}
              </p>
            ) : (
              <p className="text-[12px] text-white/25">No clip selected</p>
            )}
            {clips.length > 0 && (
              <span className="ml-auto shrink-0 text-[10px] tabular-nums text-white/25">
                {(selectedClip?.index ?? 0) + 1}&thinsp;/&thinsp;{clips.length}
              </span>
            )}
          </div>

          <div className="relative min-h-0 flex-1 overflow-hidden bg-black/25">
            {previewSrc ? (
              <img
                key={previewSrc}
                src={previewSrc}
                alt={displayName ?? "Clip preview"}
                className="h-full w-full object-contain"
                draggable={false}
              />
            ) : (
              <div className="flex h-full flex-col items-center justify-center gap-3">
                <div className="flex size-14 items-center justify-center rounded-xl border border-white/6 bg-white/3">
                  <Film className="size-6 text-white/20" />
                </div>
                <p className="text-[11px] text-white/22">
                  {clips.length === 0
                    ? "This project has no clips."
                    : "No thumbnail available for this clip."}
                </p>
              </div>
            )}
          </div>

          <div className="h-24 shrink-0 overflow-x-auto overflow-y-hidden border-t border-white/5">
            {clips.length === 0 ? (
              <div className="flex h-full items-center justify-center">
                <p className="text-[11px] text-white/25">No clips in this project.</p>
              </div>
            ) : (
              <div className="flex h-full min-w-full">
                {clips.map((clip) => (
                  <ClipBlock
                    key={`${clip.index}-${clip.path ?? clip.uid ?? clip.index}`}
                    clip={clip}
                    isSelected={selectedClip != null && clip.index === selectedClip.index}
                    totalClips={clips.length}
                    onSelect={setSelectedClipIndex}
                  />
                ))}
              </div>
            )}
          </div>
        </>
      )}

    </div>
  )
}
