import { useNavigate } from "react-router-dom"
import { AlertTriangle, LayoutTemplate, Sparkles } from "lucide-react"

import { Button } from "@/components/ui/button"
import { Card } from "@/components/ui/card"
import { useProjectStore } from "@/store/project-store"

export const NativeEditorHomePage = () => {
  const navigate = useNavigate()
  const payload = useProjectStore((s) => s.payload)
  const selectedProject = useProjectStore((s) => s.selectedProject)

  return (
    <div className="flex h-full flex-col p-6">
      <div className="grid grid-cols-2 gap-4">
        <Card className="border-white/6 bg-white/2 p-5">
          <div className="flex items-start gap-3">
            <div className="mt-0.5 rounded-md border border-amber-300/25 bg-amber-400/10 p-2">
              <LayoutTemplate className="size-4 text-amber-300/75" />
            </div>
            <div className="min-w-0">
              <h2 className="text-[12px] font-semibold uppercase tracking-[0.15em] text-white/85">
                Native-Like Flow Preview
              </h2>
              <p className="mt-1 text-[11px] leading-relaxed text-white/45">
                This interface mirrors Rockstar Editor structure with routed pages for project loading, montage editing, clip management, and gallery workflows.
              </p>
            </div>
          </div>
        </Card>

        <Card className="border-white/6 bg-white/2 p-5">
          <div className="flex items-start gap-3">
            <div className="mt-0.5 rounded-md border border-red-300/20 bg-red-500/10 p-2">
              <AlertTriangle className="size-4 text-red-300/70" />
            </div>
            <div className="min-w-0">
              <h2 className="text-[12px] font-semibold uppercase tracking-[0.15em] text-white/85">
                Placeholder Build
              </h2>
              <p className="mt-1 text-[11px] leading-relaxed text-white/45">
                Timeline actions are currently presentation-only. Use this build to validate layout, information hierarchy, and in-game readability before wiring final editor commands.
              </p>
            </div>
          </div>
        </Card>
      </div>

      <Card className="mt-4 flex-1 border-white/6 bg-black/18 p-6">
        <div className="flex h-full flex-col justify-between">
          <div>
            <p className="text-[10px] font-semibold uppercase tracking-[0.22em] text-white/22">
              Current Session
            </p>
            <h3 className="mt-2 text-[24px] leading-none font-semibold tracking-[-0.02em] text-white/92">
              {selectedProject?.projectName || "No montage selected"}
            </h3>
            <p className="mt-3 max-w-2xl text-[12px] leading-relaxed text-white/40">
              Start by loading projects from the native replay payload, then inspect montage layout and placeholder tracks in the Montage Editor page.
            </p>
          </div>

          <div className="flex items-end justify-between gap-4">
            <div className="rounded-lg border border-white/7 bg-white/2 px-4 py-3">
              <p className="text-[10px] uppercase tracking-[0.16em] text-white/25">Project Count</p>
              <p className="mt-1 text-[20px] font-semibold text-white/85">{payload?.projectCount ?? 0}</p>
            </div>

            <div className="flex items-center gap-2">
              <Button
                variant="ghost"
                className="border border-white/10 bg-white/3 text-white/78 hover:bg-white/6"
                onClick={() => navigate("/editor/projects")}
              >
                Load Project List
              </Button>
              <Button
                variant="ghost"
                className="border border-amber-300/22 bg-amber-400/10 text-amber-300/90 hover:bg-amber-300/15"
                onClick={() => navigate("/editor/projects")}
              >
                <Sparkles className="size-4" />
                Load Project
              </Button>
            </div>
          </div>
        </div>
      </Card>
    </div>
  )
}
