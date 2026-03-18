import { TooltipProvider } from "@/components/ui/tooltip"
import { Card } from "@/components/ui/card"
import { Separator } from "@/components/ui/separator"

import { ProjectEditorHeader } from "./header"
import { ProjectListPanel } from "./project-list-panel"
import { TimelinePanel } from "./timeline-panel"

export const ProjectEditorPage = () => {
  return (
    <TooltipProvider delayDuration={300}>
      <div className="w-[1440px]">
        <Card className="flex flex-col gap-0 rounded-2xl border-white/6.5 bg-[#0c1016] py-0 ring-0 shadow-[0_40px_100px_rgba(0,0,0,0.85)]">

          <ProjectEditorHeader />

          <Separator className="mx-5 bg-white/5.5" />

          <div className="flex h-[580px] overflow-hidden">

            <div className="flex h-full w-64 shrink-0 flex-col overflow-hidden">
              <ProjectListPanel />
            </div>

            <Separator orientation="vertical" className="bg-white/5.5" />

            <div className="flex h-full min-w-0 flex-1 flex-col overflow-hidden">
              <TimelinePanel />
            </div>

          </div>

          <Separator className="bg-white/5" />

          <footer className="px-8 pb-6 pt-1.5 text-center">
            <p className="text-[10px] uppercase tracking-[0.18em] text-white/10">
              Made with love
            </p>
          </footer>

        </Card>
      </div>
    </TooltipProvider>
  )
}
