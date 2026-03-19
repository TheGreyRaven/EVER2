import { NavLink, Outlet, useNavigate } from "react-router-dom"

import { sendEditorCommand } from "@/components/rockstar-editor/data"
import { Button } from "@/components/ui/button"
import { ScrollArea } from "@/components/ui/scroll-area"
import { Separator } from "@/components/ui/separator"
import { cn } from "@/lib/utils"
import { useProjectStore } from "@/store/project-store"
import { primaryNav, systemNav, type NativeNavItem } from "./navigation"

const NavButton = ({ item }: { item: NativeNavItem }) => {
  const Icon = item.icon
  const navigate = useNavigate()
  const shouldTrackActive = item.trackActive ?? true

  const handleNavigation = () => {
    item.onClick?.()

    if (item.title === "Quit Game") {
      sendEditorCommand("quit_game")
    }

    if (item.title === "Exit Rockstar Editor") {
      sendEditorCommand("exit_rockstar_editor")
    }

    navigate(item.path)
  }

  const renderContent = (isTrackedActive: boolean) => (
    <Button
      variant="ghost"
      className={cn(
        "group h-auto w-full justify-start gap-3 rounded-xl border px-3 py-2.5 text-left transition-colors",
        "border-white/6 bg-white/2 hover:bg-white/5",
        isTrackedActive && "border-amber-400/25 bg-amber-400/10",
        item.accent === "danger" && "hover:border-red-300/20 hover:bg-red-500/10",
        item.accent === "warning" && "hover:border-amber-300/20 hover:bg-amber-400/8",
      )}
    >
      <span
        className={cn(
          "flex size-8 shrink-0 items-center justify-center rounded-md border border-white/8 bg-white/4",
          isTrackedActive && "border-amber-300/35 bg-amber-300/12",
        )}
      >
        <Icon
          className={cn(
            "size-4 text-white/35",
            isTrackedActive && "text-amber-300/85",
            item.accent === "danger" && "group-hover:text-red-300/80",
          )}
        />
      </span>

      <span className="min-w-0">
        <span
          className={cn(
            "block truncate text-[11px] font-semibold uppercase tracking-[0.13em] text-white/65",
            isTrackedActive && "text-white/90",
          )}
        >
          {item.title}
        </span>
        <span className="mt-0.5 block truncate text-[10px] text-white/28">
          {item.subtitle}
        </span>
      </span>
    </Button>
  )

  if (!shouldTrackActive) {
    return (
      <button type="button" className="block w-full" onClick={handleNavigation}>
        {renderContent(false)}
      </button>
    )
  }

  return (
    <NavLink to={item.path} onClick={handleNavigation} className="block">
      {({ isActive }) => renderContent(isActive)}
    </NavLink>
  )
}

export const NativeEditorLayout = () => {
  const payload = useProjectStore((s) => s.payload)
  const selectedProject = useProjectStore((s) => s.selectedProject)

  return (
    <div className="w-[1680px] overflow-hidden rounded-2xl border border-white/6 bg-[#0c1016]/95 shadow-[0_40px_120px_rgba(0,0,0,0.88)] backdrop-blur-[2px]">
      <header className="relative border-b border-white/6 px-8 pb-5 pt-7">
        <div className="pointer-events-none absolute inset-0 bg-linear-to-b from-amber-400/5 via-amber-300/2 to-transparent" />
        <div className="relative flex items-end justify-between gap-8">
          <div>
            <p className="text-[10px] font-semibold uppercase tracking-[0.25em] text-white/35">
              Rockstar Editor Replacement
            </p>
            <h1 className="mt-2 text-[38px] leading-none font-bold tracking-[-0.03em] text-white">
              EVER<span className="text-[#ffba00]">2</span>
            </h1>
          </div>

          <div className="min-w-0 text-right">
            <p className="truncate text-[11px] font-semibold uppercase tracking-[0.2em] text-amber-400/65">
              {selectedProject?.projectName || "No Project Loaded"}
            </p>
            <p className="mt-1 text-[10px] text-white/30">
              {payload ? `${payload.projectCount} projects available` : "Awaiting native data"}
            </p>
          </div>
        </div>
      </header>

      <div className="flex h-[790px] min-h-0">
        <aside className="w-84 shrink-0 border-r border-white/6 bg-black/8">
          <ScrollArea className="h-full px-4 py-4">
            <p className="px-1 pb-2 text-[10px] font-semibold uppercase tracking-[0.22em] text-white/18">
              Editor
            </p>
            <div className="space-y-3">
              {primaryNav.map((item) => (
                <NavButton key={item.title} item={item} />
              ))}
            </div>

            {systemNav.length > 0 && (
              <>
                <Separator className="my-4 bg-white/6" />

                <p className="px-1 pb-2 text-[10px] font-semibold uppercase tracking-[0.22em] text-white/18">
                  System
                </p>
                <div className="space-y-3">
                  {systemNav.map((item) => (
                    <NavButton key={item.title} item={item} />
                  ))}
                </div>
              </>
            )}
          </ScrollArea>
        </aside>

        <main className="min-w-0 flex-1 bg-[#0b1016]">
          <Outlet />
        </main>
      </div>
    </div>
  )
}
