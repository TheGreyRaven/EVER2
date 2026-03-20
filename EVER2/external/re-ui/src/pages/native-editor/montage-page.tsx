import { useMemo, useState } from "react"
import { useNavigate } from "react-router-dom"
import {
  ArrowLeft,
  FolderOpen,
  Plus,
  Save,
} from "lucide-react"

import { sendEditorCommand } from "@/components/rockstar-editor/data"
import { Card } from "@/components/ui/card"
import { Button } from "@/components/ui/button"
import { Separator } from "@/components/ui/separator"
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import { useProjectStore } from "@/store/project-store"
import { formatDuration } from "@/pages/project-editor/utils"
import { cn } from "@/lib/utils"
import { TimelinePanel } from "@/pages/project-editor/timeline-panel"
import type { AvailableClipData } from "@/components/rockstar-editor/types"

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

const getAvailableClipDisplayName = (clip: AvailableClipData) =>
  clip.baseName ??
  (clip.path ? clip.path.split(/[\\/]/).pop()?.replace(/\.[^.]+$/, "") ?? null : null) ??
  `UID ${clip.uid}`

type SourceClipOption = {
  key: string
  label: string
  sourceIndex?: number
  baseName?: string
  ownerIdText?: string
  path?: string
}

export const NativeMontagePage = () => {
  const navigate = useNavigate()
  const payload = useProjectStore((s) => s.payload)
  const selectedProject = useProjectStore((s) => s.selectedProject)
  const lastCommandResponse = useProjectStore((s) => s.lastCommandResponse)
  const [selectedSourceKey, setSelectedSourceKey] = useState<string>("")

  const clips = useMemo(() => selectedProject?.clips ?? [], [selectedProject])

  const totalMs = selectedProject?.durationMs ?? 0

  const sourceOptions = useMemo<SourceClipOption[]>(() => {
    const global = payload?.availableClips ?? []
    return global.map((clip) => ({
      key: `global-${clip.uid}`,
      label: getAvailableClipDisplayName(clip),
      sourceIndex: clip.sourceIndex,
      baseName: clip.baseName,
      ownerIdText: clip.ownerIdText,
      path: clip.path,
    }))
  }, [payload])

  const selectedSourceOption = useMemo<SourceClipOption | null>(() => {
    if (sourceOptions.length === 0) {
      return null
    }

    if (!selectedSourceKey) {
      return sourceOptions[0]
    }

    return sourceOptions.find((option) => option.key === selectedSourceKey) ?? sourceOptions[0]
  }, [selectedSourceKey, sourceOptions])

  const commandStatusText = useMemo(() => {
    if (!lastCommandResponse) {
      return "No command status yet."
    }

    const base = `${lastCommandResponse.action}: ${lastCommandResponse.status}`
    if (lastCommandResponse.message) {
      return `${base} - ${lastCommandResponse.message}`
    }

    return base
  }, [lastCommandResponse])

  const handleAddClip = () => {
    if (!selectedProject || !selectedSourceOption) {
      return
    }

    const commandPayload: Record<string, unknown> = {
      projectIndex: selectedProject.index,
      destinationIndex: selectedProject.clips.length,
    }

    if (selectedSourceOption.sourceIndex != null) {
      commandPayload.sourceClipIndex = selectedSourceOption.sourceIndex
    }

    if (selectedSourceOption.baseName && selectedSourceOption.ownerIdText) {
      commandPayload.sourceClipBaseName = selectedSourceOption.baseName
      commandPayload.sourceClipOwnerIdText = selectedSourceOption.ownerIdText
    }

    if (selectedSourceOption.path) {
      commandPayload.sourceClipPath = selectedSourceOption.path
    }

    sendEditorCommand("add_clip_to_project", commandPayload)
  }

  const handleSaveProject = () => {
    if (!selectedProject) {
      return
    }

    sendEditorCommand("save_project", {
      projectIndex: selectedProject.index,
    })
  }

  const videoLaneItems = useMemo<LaneItem[]>(() => {
    if (!selectedProject || clips.length === 0) {
      return []
    }

    const clipDuration = Math.max(6000, Math.floor(totalMs / Math.max(clips.length, 1)))

    return clips.map((clip, index) => ({
      id: `video-${clip.index}`,
      title: clip.baseName || `Clip ${index + 1}`,
      startMs: index * clipDuration,
      durationMs: clipDuration,
    }))
  }, [clips, selectedProject, totalMs])

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

  return (
    <div className="w-420 overflow-hidden rounded-2xl border border-white/6 bg-[#0c1016]/95 shadow-[0_40px_120px_rgba(0,0,0,0.88)] backdrop-blur-[2px]">
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

      <div className="grid h-197.5 min-h-0 grid-cols-[1fr_330px] gap-4 p-6">
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

          <div className="max-h-70 min-h-55 overflow-hidden">
            <div className="grid grid-cols-[170px_1fr] border-b border-white/6 bg-black/24">
              <div className="border-r border-white/6 px-3 py-2 text-[10px] font-semibold uppercase tracking-[0.2em] text-white/35">
                Track
              </div>
              <div className="px-3 py-2 text-[10px] font-semibold uppercase tracking-[0.2em] text-white/25">
                Timeline Video Lane
              </div>
            </div>

            <div className="h-57.5 overflow-y-auto">
              <TrackLane
                laneLabel="Video"
                accentClass="border-amber-300/45 bg-amber-300/20 text-amber-100/90"
                items={videoLaneItems}
                totalMs={totalMs}
              />
            </div>
          </div>
        </Card>

        <Card className="flex min-h-0 flex-col border-white/6 bg-black/22 p-4">
          <p className="text-[10px] font-semibold uppercase tracking-[0.2em] text-white/22">
            Montage Actions
          </p>
          <p className="mt-1 text-[11px] text-white/42">
            Native bridge actions for clip insertion and project save.
          </p>

          <div className="mt-4 rounded-lg border border-white/7 bg-white/2 p-3">
            <p className="text-[10px] font-semibold uppercase tracking-[0.16em] text-white/22">
              Add Clip To Timeline
            </p>
            <p className="mt-2 text-[11px] text-white/40">
              Select a source clip and append it to the end of the project timeline.
            </p>

            <Select
              value={selectedSourceOption?.key ?? ""}
              onValueChange={setSelectedSourceKey}
              disabled={sourceOptions.length === 0}
            >
              <SelectTrigger className="mt-3 h-9 w-full border-white/10 bg-black/25 text-[11px] text-white/80">
                <SelectValue placeholder="Select source clip" />
              </SelectTrigger>
              <SelectContent className="max-h-64 border-white/12 bg-[#101821] text-[11px] text-white/85">
                {sourceOptions.map((option) => (
                  <SelectItem key={option.key} value={option.key} className="text-[11px]">
                    {option.label}
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>

            {sourceOptions.length <= 1 && (
              <p className="mt-2 text-[10px] text-white/35">
                {sourceOptions.length === 0
                  ? "No available clips from native clip cache yet. Load project again after clip enumeration completes."
                  : "Only one source clip is currently available."}
              </p>
            )}

            <Button
              variant="ghost"
              className="mt-3 w-full border border-amber-300/25 bg-amber-300/10 text-amber-200/90 hover:bg-amber-300/15"
              onClick={handleAddClip}
              disabled={selectedSourceOption == null}
            >
              <Plus className="size-4" />
              Add clip
            </Button>
          </div>

          <Separator className="my-4 bg-white/6" />

          <div className="rounded-lg border border-white/7 bg-white/2 p-3">
            <p className="text-[10px] font-semibold uppercase tracking-[0.16em] text-white/22">
              Save Project
            </p>

            <Button
              variant="ghost"
              className="mt-3 w-full border border-cyan-300/25 bg-cyan-300/10 text-cyan-100/90 hover:bg-cyan-300/15"
              onClick={handleSaveProject}
            >
              <Save className="size-4" />
              Save project
            </Button>

            <p className="mt-3 text-[10px] leading-relaxed text-white/40">
              {commandStatusText}
            </p>
          </div>
        </Card>
      </div>
    </div>
  )
}
