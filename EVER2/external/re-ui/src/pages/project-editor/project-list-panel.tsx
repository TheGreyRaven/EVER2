import { Film, Loader2, RefreshCw } from "lucide-react"

import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { ScrollArea } from "@/components/ui/scroll-area"
import { cn } from "@/lib/utils"
import { useProjectStore } from "@/store/project-store"
import { sendEditorCommand } from "@/components/rockstar-editor/data"
import { diskPathToFileUrl, formatDuration } from "./utils"

export const ProjectListPanel = () => {
  const payload = useProjectStore((s) => s.payload)
  const selectedProject = useProjectStore((s) => s.selectedProject)
  const isLoading = useProjectStore((s) => s.isLoading)
  const selectProject = useProjectStore((s) => s.selectProject)
  const setLoading = useProjectStore((s) => s.setLoading)
  const reset = useProjectStore((s) => s.reset)

  const handleRefresh = () => {
    reset()
    setLoading(true)
    sendEditorCommand("load_project")
  }

  return (
    <div className="flex h-full flex-col overflow-hidden">
      <div className="flex items-center justify-between px-5 pb-2 pt-4">
        <p className="text-[10px] font-semibold uppercase tracking-[0.22em] text-white/18">
          Projects
        </p>
        {payload != null && (
          <Button
            variant="ghost"
            size="icon-xs"
            onClick={handleRefresh}
            title="Refresh project list"
            className="text-white/25 hover:text-white/60"
          >
            <RefreshCw className="size-3" />
          </Button>
        )}
      </div>

      {isLoading && payload == null && (
        <div className="flex flex-1 flex-col items-center justify-center gap-3 px-6 py-8 text-center">
          <Loader2 className="size-5 animate-spin text-amber-400/40" />
          <p className="text-[11px] leading-relaxed text-white/28">
            Loading projects from native replay system…
          </p>
        </div>
      )}

      {!isLoading && payload == null && (
        <div className="flex flex-1 flex-col items-center justify-center gap-3 px-6 py-8 text-center">
          <div className="mb-1 flex size-10 items-center justify-center rounded-xl border border-white/6 bg-white/3">
            <Film className="size-5 text-white/20" />
          </div>
          <p className="text-[11px] leading-relaxed text-white/28">
            No projects loaded yet.
          </p>
        </div>
      )}

      {payload != null && (
        <ScrollArea className="flex-1">
          <div className="space-y-1 px-3 pb-3">
            {payload.projects.length === 0 && (
              <p className="px-2 py-4 text-center text-[11px] text-white/28">
                No projects found in the replay system.
              </p>
            )}
            {payload.projects.map((project) => {
              const isSelected = selectedProject?.index === project.index
              return (
                <button
                  key={`${project.index}-${project.path}`}
                  onClick={() => selectProject(project)}
                  className={cn(
                    "group relative w-full overflow-hidden rounded-xl border px-3 py-2.5 text-left transition-all duration-150",
                    isSelected
                      ? "border-amber-400/25 bg-amber-400/8"
                      : "border-white/5 bg-white/2 hover:border-white/10 hover:bg-white/4",
                  )}
                >
                  {isSelected && (
                    <span
                      aria-hidden="true"
                      className="absolute inset-y-0 left-0 w-0.5 bg-amber-400/70"
                    />
                  )}

                  <div className="flex items-start gap-2.5">
                    <div className="relative shrink-0 overflow-hidden rounded-md border border-white/8 bg-white/4" style={{ width: 56, height: 32 }}>
                      {project.previewExists && project.previewDiskPath ? (
                        <img
                          src={diskPathToFileUrl(project.previewDiskPath)}
                          alt={project.projectName || "Preview"}
                          className="h-full w-full object-cover"
                          draggable={false}
                        />
                      ) : (
                        <div className="flex h-full items-center justify-center">
                          <Film className="size-3 text-white/18" />
                        </div>
                      )}
                    </div>

                    <div className="min-w-0 flex-1">
                      <div className="flex items-start justify-between gap-1.5">
                        <p
                          className={cn(
                            "truncate text-[11.5px] font-semibold leading-snug",
                            isSelected ? "text-white/88" : "text-white/55",
                          )}
                        >
                          {project.projectName || "Unnamed Project"}
                        </p>
                        {project.corrupt && (
                          <Badge
                            variant="destructive"
                            className="shrink-0 text-[9px]"
                          >
                            Corrupt
                          </Badge>
                        )}
                      </div>

                      <div className="mt-1 flex items-center gap-1.5">
                        <span
                          className={cn(
                            "text-[10px]",
                            isSelected ? "text-amber-400/60" : "text-white/28",
                          )}
                        >
                          {project.clipCount}{" "}
                          {project.clipCount === 1 ? "clip" : "clips"}
                        </span>
                        <span className="text-[10px] text-white/15">·</span>
                        <span
                          className={cn(
                            "text-[10px]",
                            isSelected ? "text-white/45" : "text-white/25",
                          )}
                        >
                          {formatDuration(project.durationMs)}
                        </span>
                      </div>
                    </div>
                  </div>
                </button>
              )
            })}
          </div>
        </ScrollArea>
      )}

      {payload != null && (
        <div className="border-t border-white/5 px-5 py-2.5">
          <p className="text-[10px] text-white/20">
            {payload.projectCount}{" "}
            {payload.projectCount === 1 ? "project" : "projects"} found
          </p>
        </div>
      )}
    </div>
  )
}
