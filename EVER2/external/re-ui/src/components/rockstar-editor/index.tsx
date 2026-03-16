import { Card } from "@/components/ui/card"
import { exitItems, primaryItems } from "./data"
import { EditorFooter } from "./footer"
import { EditorHeader } from "./header"
import { InfoPanel } from "./info-panel"
import { MenuItem } from "./menu-item"
import { Separator } from "@/components/ui/separator"

export const RockstarEditor = () => {
  return (
    <Card className="gap-0 rounded-2xl border-white/6.5 bg-[#0c1016] py-0 ring-0 shadow-[0_40px_100px_rgba(0,0,0,0.85)]">
      <EditorHeader />

      <Separator className="mx-5 bg-white/5.5" />

      <div className="flex">
        <div className="flex w-110 shrink-0 flex-col">
          <div className="flex-1 pt-3 pb-1">
            <p className="px-5 pb-2 pt-1 text-[10px] font-semibold uppercase tracking-[0.22em] text-white/18">
              Navigation
            </p>
            <nav aria-label="Editor navigation">
              {primaryItems.map((item) => (
                <MenuItem key={item.label} item={item} />
              ))}
            </nav>
          </div>

          <div className="pb-3">
            <p className="px-5 pb-2 pt-3 text-[10px] font-semibold uppercase tracking-[0.22em] text-white/18">
              System
            </p>
            <nav aria-label="Exit options">
              {exitItems.map((item) => (
                <MenuItem key={item.label} item={item} />
              ))}
            </nav>
          </div>

        </div>

        <Separator orientation="vertical" className="bg-white/5.5" />

        <div className="min-w-0 flex-1">
          <InfoPanel />
        </div>

      </div>

      <Separator />

      <EditorFooter />
    </Card>
  )
}
