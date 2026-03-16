import { TriangleAlert } from "lucide-react"

import { primaryItems } from "./data"

export const InfoPanel = () => {
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

    </aside>
  )
}
