import { useMemo, useState } from "react"
import { useNavigate } from "react-router-dom"
import {
  ArrowLeft,
  CopyPlus,
  FileMusic,
  FolderOpen,
  Radio,
  Subtitles,
  Trash2,
  Waves,
} from "lucide-react"

import { Card } from "@/components/ui/card"
import { Button } from "@/components/ui/button"
import { Separator } from "@/components/ui/separator"
import { ScrollArea } from "@/components/ui/scroll-area"
import { useProjectStore } from "@/store/project-store"
import { formatDuration } from "@/pages/project-editor/utils"
import { cn } from "@/lib/utils"
import { TimelinePanel } from "@/pages/project-editor/timeline-panel"

type LaneItem = {
  id: string
  title: string
  startMs: number
  durationMs: number
}

const TrackLane = ({
  laneLabel,
  accentClass,
  items,
  totalMs,
}: {
  laneLabel: string
  accentClass: string
  items: LaneItem[]
  totalMs: number
}) => {
  return (
    <div className="grid grid-cols-[170px_1fr] border-b border-white/6 last:border-b-0">
      <div className="flex items-center border-r border-white/6 bg-black/28 px-3 py-2">
        <p className="text-[10px] font-semibold uppercase tracking-[0.2em] text-white/35">
          {laneLabel}
        </p>
      </div>

      <div className="relative h-12 bg-white/2">
        {items.length === 0 && (
          <span className="absolute left-3 top-1/2 -translate-y-1/2 text-[10px] text-white/20">
            Empty
          </span>
        )}

        {items.map((item) => {
          const left = totalMs > 0 ? (item.startMs / totalMs) * 100 : 0
          const width = totalMs > 0 ? Math.max((item.durationMs / totalMs) * 100, 2) : 6

          return (
            <div
              key={item.id}
              className={cn(
                "absolute top-1/2 h-8 -translate-y-1/2 rounded-md border px-2",
                "flex items-center overflow-hidden text-[10px] font-semibold tracking-[0.04em]",
                accentClass,
              )}
              style={{ left: `${left}%`, width: `${width}%` }}
              title={`${item.title} (${formatDuration(item.durationMs)})`}
            >
              <span className="truncate">{item.title}</span>
            </div>
          )
        })}
      </div>
    </div>
  )
}

const actionButtons = [
  { icon: CopyPlus, label: "Duplicate Clip" },
  { icon: Trash2, label: "Delete Clip" },
  { icon: Waves, label: "Add Ambient" },
  { icon: Radio, label: "Add Radio" },
  { icon: FileMusic, label: "Add Score" },
  { icon: Subtitles, label: "Add Text" },
]

export const NativeMontagePage = () => {
  const navigate = useNavigate()
  const selectedProject = useProjectStore((s) => s.selectedProject)
  const [activeAction, setActiveAction] = useState<string>("Duplicate Clip")

  if (!selectedProject) {
    return (
      <div className="flex h-full p-6">
        <Card className="flex flex-1 items-center justify-center border-white/6 bg-black/24 p-8">
          <div className="max-w-lg text-center">
            <div className="mx-auto mb-4 flex size-14 items-center justify-center rounded-xl border border-white/8 bg-white/3">
              <FolderOpen className="size-6 text-white/28" />
            </div>
            <h2 className="text-[24px] font-semibold tracking-[-0.02em] text-white/88">
              Load a project first
            </h2>
            <p className="mt-3 text-[12px] leading-relaxed text-white/42">
              The Rockstar flow requires loading a project before editing montage tracks.
              Open the Load Project page, select a project, then use Edit project.
            </p>
            <Button
              variant="ghost"
              className="mt-5 border border-amber-300/25 bg-amber-400/10 text-amber-300/90 hover:bg-amber-300/15"
              onClick={() => navigate("/editor/projects")}
            >
              Go to Load Project
            </Button>
          </div>
        </Card>
      </div>
    )
  }

  const totalMs = selectedProject.durationMs

  const videoLaneItems = useMemo<LaneItem[]>(() => {
    if (!selectedProject || selectedProject.clips.length === 0) {
      return []
    }

    const clipDuration = Math.max(6000, Math.floor(totalMs / Math.max(selectedProject.clips.length, 1)))

    return selectedProject.clips.map((clip, index) => ({
      id: `video-${clip.index}`,
      title: clip.baseName || `Clip ${index + 1}`,
      startMs: index * clipDuration,
      durationMs: clipDuration,
    }))
  }, [selectedProject, totalMs])

  const ambientLaneItems = useMemo<LaneItem[]>(
    () =>
      selectedProject
        ? [
            {
              id: "ambient-a",
              title: "City Ambience",
              startMs: 4000,
              durationMs: Math.max(14_000, Math.floor(totalMs * 0.34)),
            },
          ]
        : [],
    [selectedProject, totalMs],
  )

  const musicLaneItems = useMemo<LaneItem[]>(
    () =>
      selectedProject
        ? [
            {
              id: "music-a",
              title: "Radio Track Placeholder",
              startMs: 10_000,
              durationMs: Math.max(18_000, Math.floor(totalMs * 0.4)),
            },
            {
              id: "music-b",
              title: "Score Layer Placeholder",
              startMs: Math.max(30_000, Math.floor(totalMs * 0.45)),
              durationMs: Math.max(12_000, Math.floor(totalMs * 0.25)),
            },
          ]
        : [],
    [selectedProject, totalMs],
  )

  const textLaneItems = useMemo<LaneItem[]>(
    () =>
      selectedProject
        ? [
            {
              id: "text-a",
              title: "Intro Title",
              startMs: 3000,
              durationMs: 6000,
            },
            {
              id: "text-b",
              title: "Outro Card",
              startMs: Math.max(20_000, totalMs - 10_000),
              durationMs: 4500,
            },
          ]
        : [],
    [selectedProject, totalMs],
  )

  return (
    <div className="w-[1680px] overflow-hidden rounded-2xl border border-white/6 bg-[#0c1016]/95 shadow-[0_40px_120px_rgba(0,0,0,0.88)] backdrop-blur-[2px]">
      <header className="relative border-b border-white/6 px-8 pb-5 pt-7">
        <div className="pointer-events-none absolute inset-0 bg-linear-to-b from-amber-400/5 via-amber-300/2 to-transparent" />
        <div className="relative flex items-end justify-between gap-8">
          <div className="flex items-end gap-3">
            <Button
              variant="ghost"
              className="mb-0.5 border border-white/10 bg-white/4 text-white/75 hover:bg-white/8"
              onClick={() => navigate("/editor/projects")}
            >
              <ArrowLeft className="size-4" />
              Back
            </Button>
            <div>
              <p className="text-[10px] font-semibold uppercase tracking-[0.25em] text-white/35">
                Montage Editor
              </p>
              <h1 className="mt-2 text-[38px] leading-none font-bold tracking-[-0.03em] text-white">
                EVER<span className="text-[#ffba00]">2</span>
              </h1>
            </div>
          </div>

          <div className="min-w-0 text-right">
            <p className="truncate text-[11px] font-semibold uppercase tracking-[0.2em] text-amber-400/65">
              {selectedProject.projectName}
            </p>
            <p className="mt-1 text-[10px] text-white/30">
              {selectedProject.clipCount} clips loaded
            </p>
          </div>
        </div>
      </header>

      <div className="grid h-[790px] min-h-0 grid-cols-[1fr_330px] gap-4 p-6">
        <Card className="flex min-h-0 flex-col border-white/6 bg-black/22 p-0">
          <div className="flex items-center justify-between border-b border-white/6 px-4 py-3">
            <div>
              <p className="text-[10px] font-semibold uppercase tracking-[0.2em] text-white/22">
                Stage Preview
              </p>
              <p className="mt-1 text-[11px] text-white/45">
                {selectedProject
                  ? `${selectedProject.projectName} · ${selectedProject.clipCount} clips`
                  : "Load a project to preview montage lanes"}
              </p>
            </div>

            <span className="text-[11px] font-semibold text-amber-300/75">
              {formatDuration(totalMs)}
            </span>
          </div>

          <div className="min-h-0 flex-1 border-b border-white/6 bg-black/40">
            <TimelinePanel />
          </div>

          <div className="max-h-[280px] min-h-[220px] overflow-hidden">
            <div className="grid grid-cols-[170px_1fr] border-b border-white/6 bg-black/24">
              <div className="border-r border-white/6 px-3 py-2 text-[10px] font-semibold uppercase tracking-[0.2em] text-white/35">
                Track
              </div>
              <div className="px-3 py-2 text-[10px] font-semibold uppercase tracking-[0.2em] text-white/25">
                Timeline Lanes (Placeholder)
              </div>
            </div>

            <ScrollArea className="h-[230px]">
              <TrackLane
                laneLabel="Video"
                accentClass="border-amber-300/45 bg-amber-300/20 text-amber-100/90"
                items={videoLaneItems}
                totalMs={totalMs}
              />
              <TrackLane
                laneLabel="Ambient"
                accentClass="border-cyan-300/45 bg-cyan-300/18 text-cyan-100/90"
                items={ambientLaneItems}
                totalMs={totalMs}
              />
              <TrackLane
                laneLabel="Radio + Score"
                accentClass="border-orange-300/45 bg-orange-300/18 text-orange-100/90"
                items={musicLaneItems}
                totalMs={totalMs}
              />
              <TrackLane
                laneLabel="Text"
                accentClass="border-violet-300/45 bg-violet-300/18 text-violet-100/90"
                items={textLaneItems}
                totalMs={totalMs}
              />
            </ScrollArea>
          </div>
        </Card>

        <Card className="flex min-h-0 flex-col border-white/6 bg-black/22 p-4">
          <p className="text-[10px] font-semibold uppercase tracking-[0.2em] text-white/22">
            Montage Actions
          </p>
          <p className="mt-1 text-[11px] text-white/42">
            Placeholder command panel based on Rockstar Editor operation flow.
          </p>

          <div className="mt-4 space-y-2">
            {actionButtons.map((action) => {
              const Icon = action.icon
              const isActive = action.label === activeAction

              return (
                <Button
                  key={action.label}
                  variant="ghost"
                  className={cn(
                    "h-auto w-full justify-start gap-2.5 rounded-lg border px-3 py-2.5 text-left",
                    isActive
                      ? "border-amber-300/25 bg-amber-300/10 text-amber-200/90"
                      : "border-white/8 bg-white/2 text-white/65 hover:bg-white/5",
                  )}
                  onClick={() => setActiveAction(action.label)}
                >
                  <Icon className="size-4" />
                  <span className="text-[11px] font-semibold uppercase tracking-[0.11em]">
                    {action.label}
                  </span>
                </Button>
              )
            })}
          </div>

          <Separator className="my-4 bg-white/6" />

          <div className="rounded-lg border border-white/7 bg-white/2 p-3">
            <p className="text-[10px] font-semibold uppercase tracking-[0.16em] text-white/22">
              Active Placeholder Action
            </p>
            <p className="mt-2 text-[12px] font-semibold text-white/85">{activeAction}</p>
            <p className="mt-2 text-[11px] leading-relaxed text-white/42">
              This panel is UI-only for now. Native function calls can later be routed through your CEF bridge and mapped directly to project wrapper hooks.
            </p>
          </div>
        </Card>
      </div>
    </div>
  )
}
