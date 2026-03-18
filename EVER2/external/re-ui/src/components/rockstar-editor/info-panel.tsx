import { TriangleAlert } from "lucide-react"

import { primaryItems } from "./data"
import type { ReplayProjectsPayload } from "./types"

type InfoPanelProps = {
  projectsPayload: ReplayProjectsPayload | null
}

export const InfoPanel = ({ projectsPayload }: InfoPanelProps) => {
  const hasProjects =
    projectsPayload != null &&
    Array.isArray(projectsPayload.projects) &&
    projectsPayload.projects.length > 0

  return (
    <aside className="flex h-full flex-col">
      <div className="relative flex flex-1 flex-col items-center justify-center overflow-hidden px-8 py-8 text-center">
        <div
          aria-hidden="true"
          className="pointer-events-none absolute inset-0"
          style={{
            background:
              "radial-gradient(ellipse 70% 55% at 50% 40%, oklch(0.83 0.14 84 / 0.07) 0%, transparent 70%)",
          }}
        />

        <div className="relative mb-5 flex size-14 items-center justify-center">
          <div
            aria-hidden="true"
            className="absolute inset-0 rounded-full border border-amber-400/15 bg-amber-400/5"
          />
          <TriangleAlert className="relative size-6 text-amber-400/60" />
        </div>

        <p className="mb-3 text-[11px] font-bold uppercase tracking-[0.28em] text-amber-400/60">
          Work in Progress
        </p>

        <p className="max-w-65 text-[12px] leading-loose text-white/28">
          EVER2 is under active development. You may encounter missing
          features, visual glitches, and unexpected crashes. Proceed with
          caution.
        </p>
      </div>

      <div className="flex items-center gap-3 px-6">
        <div aria-hidden="true" className="h-px flex-1 bg-white/5" />
        <span className="text-[10px] font-semibold uppercase tracking-[0.22em] text-white/15">
          Features
        </span>
        <div aria-hidden="true" className="h-px flex-1 bg-white/5" />
      </div>

      <div className="grid grid-cols-2 gap-2.5 p-5">
        {primaryItems.map((item) => {
          const Icon = item.icon
          return (
            <div
              key={item.label}
              className="rounded-xl border border-white/5 bg-white/2 px-4 py-4"
            >
              <Icon className="mb-2.5 size-4 text-white/30" />
              <p className="mb-1.5 text-[12.5px] font-semibold leading-tight text-white/50">
                {item.label}
              </p>
              <p className="text-[11px] leading-relaxed text-white/22">
                {item.description}
              </p>
            </div>
          )
        })}
      </div>

      <div className="px-5 pb-5 pt-1">
        <div className="mb-2 flex items-center justify-between">
          <p className="text-[10px] font-semibold uppercase tracking-[0.22em] text-white/18">
            Loaded Projects
          </p>
          <p className="text-[10px] text-white/28">
            {projectsPayload?.projectCount ?? 0}
          </p>
        </div>

        {!hasProjects && (
          <div className="rounded-xl border border-white/6 bg-white/2 px-4 py-3 text-[11px] leading-relaxed text-white/28">
            Click "Load project" to fetch montage/clip data from the native replay subsystem.
          </div>
        )}

        {hasProjects && (
          <div className="max-h-56 space-y-2 overflow-y-auto pr-1">
            {projectsPayload.projects.map((project) => (
              <div
                key={`${project.index}-${project.projectName}-${project.path}`}
                className="rounded-xl border border-white/6 bg-white/2 px-3 py-2"
              >
                <p className="text-[12px] font-semibold text-white/62">
                  {project.projectName || "Unnamed project"}
                </p>
                <p className="mt-1 text-[10px] text-white/30">
                  clips: {project.clipCount} | duration: {project.durationMs}ms
                </p>

                <div className="mt-2 space-y-1">
                  {project.clips.map((clip) => (
                    <p
                      key={`${project.index}-${clip.index}-${clip.path ?? clip.uid ?? "clip"}`}
                      className="truncate text-[10px] text-white/32"
                      title={clip.path ?? ""}
                    >
                      {clip.uid != null ? `${clip.uid} -> ` : ""}
                      {clip.path ?? "<no clip path>"}
                    </p>
                  ))}
                </div>
              </div>
            ))}
          </div>
        )}
      </div>

    </aside>
  )
}
