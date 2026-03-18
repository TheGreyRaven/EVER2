import { ArrowLeft } from "lucide-react"
import { useNavigate } from "react-router-dom"

import { Button } from "@/components/ui/button"
import { useProjectStore } from "@/store/project-store"

export const ProjectEditorHeader = () => {
  const navigate = useNavigate()
  const selectedProject = useProjectStore((s) => s.selectedProject)

  return (
    <header className="relative px-8 pb-6 pt-8 text-center">
      <div
        aria-hidden="true"
        className="pointer-events-none absolute inset-0 bg-linear-to-b from-amber-400/4 to-transparent"
      />

      <Button
        variant="ghost"
        size="sm"
        onClick={() => navigate("/")}
        className="absolute left-5 top-5 gap-2 text-white/30 hover:text-white/70"
      >
        <ArrowLeft className="size-3.5" />
        <span className="text-[11px] font-semibold uppercase tracking-[0.18em]">
          Back
        </span>
      </Button>

      <h1 className="relative text-[36px] font-bold leading-none tracking-[-0.035em] text-white">
        EVER<span className="text-[#ffba00]">2</span>
      </h1>

      {selectedProject ? (
        <p className="relative mt-2.5 text-[11px] font-semibold uppercase tracking-[0.22em] text-amber-400/60">
          {selectedProject.projectName || "Unnamed Project"}
        </p>
      ) : (
        <p className="relative mt-2.5 text-[11px] font-semibold uppercase tracking-[0.25em] text-white/30">
          Project Editor
        </p>
      )}
    </header>
  )
}
