import { useNavigate } from "react-router-dom"
import { Loader2, ListFilter, Sparkles } from "lucide-react"

import { sendEditorCommand } from "@/components/rockstar-editor/data"
import { Button } from "@/components/ui/button"
import { Card } from "@/components/ui/card"
import { Separator } from "@/components/ui/separator"
import { useProjectStore } from "@/store/project-store"
import { formatDuration } from "@/pages/project-editor/utils"
import { ProjectListPanel } from "@/pages/project-editor/project-list-panel"
import { TimelinePanel } from "@/pages/project-editor/timeline-panel"
import { diskPathToFileUrl } from "@/pages/project-editor/utils"

export const NativeProjectsPage = () => {
  const navigate = useNavigate()
  const payload = useProjectStore((s) => s.payload)
  const selectedProject = useProjectStore((s) => s.selectedProject)
  const isLoading = useProjectStore((s) => s.isLoading)
  const setLoading = useProjectStore((s) => s.setLoading)

  const handleLoad = () => {
    setLoading(true)
    sendEditorCommand("load_project")
  }

  const handleEditProject = () => {
    if (!selectedProject) {
      return
    }

    sendEditorCommand("load_project", {
      nativeLoad: true,
      projectIndex: selectedProject.index,
      projectPath: selectedProject.path,
    })
    navigate("/editor/montage")
  }

  return (
    <div className="flex h-full min-h-0 gap-4 p-6">
      <Card className="flex h-full w-80 min-h-0 shrink-0 flex-col border-white/6 bg-black/22 p-0">
        <ProjectListPanel />
      </Card>

      <Card className="flex min-h-0 min-w-0 flex-1 flex-col border-white/6 bg-black/16 p-5">
        <div className="flex items-start justify-between gap-3">
          <div>
            <p className="text-[10px] font-semibold uppercase tracking-[0.2em] text-white/22">
              Project Details
            </p>
            <h2 className="mt-1 text-[22px] leading-none font-semibold text-white/88">
              {selectedProject?.projectName || "Select a project"}
            </h2>
          </div>

          <Button
            variant="ghost"
            className="border border-white/10 bg-white/2 text-white/70 hover:bg-white/5"
            onClick={handleLoad}
            disabled={isLoading}
          >
            {isLoading ? <Loader2 className="size-4 animate-spin" /> : <ListFilter className="size-4" />}
            Refresh Native Payload
          </Button>
        </div>

        <Separator className="my-4 bg-white/6" />

        {!selectedProject && (
          <div className="flex flex-1 items-center justify-center rounded-xl border border-white/7 bg-white/2 p-8 text-center text-[12px] text-white/38">
            Choose a project from the left panel to preview metadata and open the montage workspace.
          </div>
        )}

        {selectedProject && (
          <div className="flex min-h-0 flex-1 flex-col gap-4">
            <div className="grid grid-cols-4 gap-3">
              <Card className="border-white/7 bg-white/2 p-3">
                <p className="text-[10px] uppercase tracking-[0.14em] text-white/25">Clips</p>
                <p className="mt-1 text-[20px] font-semibold text-white/85">{selectedProject.clipCount}</p>
              </Card>
              <Card className="border-white/7 bg-white/2 p-3">
                <p className="text-[10px] uppercase tracking-[0.14em] text-white/25">Duration</p>
                <p className="mt-1 text-[20px] font-semibold text-white/85">{formatDuration(selectedProject.durationMs)}</p>
              </Card>
              <Card className="border-white/7 bg-white/2 p-3">
                <p className="text-[10px] uppercase tracking-[0.14em] text-white/25">Corrupt</p>
                <p className="mt-1 text-[20px] font-semibold text-white/85">{selectedProject.corrupt ? "Yes" : "No"}</p>
              </Card>
              <Card className="border-white/7 bg-white/2 p-3">
                <p className="text-[10px] uppercase tracking-[0.14em] text-white/25">Size</p>
                <p className="mt-1 text-[20px] font-semibold text-white/85">{Math.max(0, Math.round(selectedProject.sizeBytes / 1024))} KB</p>
              </Card>
            </div>

            <Card className="flex-1 border-white/7 bg-black/22 p-4">
              <p className="text-[10px] font-semibold uppercase tracking-[0.2em] text-white/24">Path</p>
              <p className="mt-2 break-all text-[11px] text-white/45">{selectedProject.path}</p>

              <p className="mt-4 text-[10px] font-semibold uppercase tracking-[0.2em] text-white/24">Preview Candidate</p>
              <p className="mt-2 break-all text-[11px] text-white/45">{selectedProject.previewCandidate || "None"}</p>

              <p className="mt-4 text-[10px] font-semibold uppercase tracking-[0.2em] text-white/24">Preview Metadata</p>
              <div className="mt-2 overflow-hidden rounded-lg border border-white/7 bg-black/35">
                {selectedProject.previewExists && selectedProject.previewDiskPath ? (
                  <img
                    src={diskPathToFileUrl(selectedProject.previewDiskPath)}
                    alt={`${selectedProject.projectName || "Project"} preview`}
                    className="h-36 w-full object-cover"
                    draggable={false}
                  />
                ) : (
                  <div className="flex h-36 items-center justify-center text-[11px] text-white/30">
                    No project thumbnail available
                  </div>
                )}
              </div>
            </Card>

            <Card className="min-h-0 flex-1 overflow-hidden border-white/7 bg-black/28 p-0">
              <div className="flex items-center justify-between border-b border-white/6 px-4 py-2.5">
                <p className="text-[10px] font-semibold uppercase tracking-[0.2em] text-white/24">
                  Clip Preview
                </p>
                <p className="text-[10px] text-white/28">
                  Project clips and thumbnails
                </p>
              </div>
              <div className="h-[300px] min-h-0">
                <TimelinePanel />
              </div>
            </Card>

            <div className="mt-auto flex items-center justify-end gap-2 pt-1">
              <Button
                variant="ghost"
                className="border border-white/10 bg-white/3 text-white/75 hover:bg-white/6"
                onClick={() => navigate("/editor/home")}
              >
                Back To Home
              </Button>
              <Button
                variant="ghost"
                className="border border-amber-300/25 bg-amber-400/10 text-amber-300/90 hover:bg-amber-300/15"
                onClick={handleEditProject}
              >
                <Sparkles className="size-4" />
                Edit project
              </Button>
            </div>
          </div>
        )}

        <p className="mt-4 text-[10px] text-white/22">
          {payload ? `Payload request successful: ${payload.projectCount} projects` : "No payload received yet"}
        </p>
      </Card>
    </div>
  )
}
