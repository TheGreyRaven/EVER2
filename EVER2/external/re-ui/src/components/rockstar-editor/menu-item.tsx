import { useNavigate } from "react-router-dom"

import { Button } from "@/components/ui/button"
import { cn } from "@/lib/utils"
import type { MenuItemConfig } from "./types"

type MenuItemProps = {
  item: MenuItemConfig
}

export const MenuItem = ({ item }: MenuItemProps) => {
  const Icon = item.icon
  const isDanger = item.variant === "danger"
  const isExit = item.variant === "exit"
  const isDefault = !isDanger && !isExit
  const navigate = useNavigate()

  const handleClick = () => {
    item.onClick?.()
    if (item.route) navigate(item.route)
  }

  return (
    <Button
      variant="ghost"
      onClick={handleClick}
      className={cn(
        "group relative h-auto w-full justify-start gap-4 overflow-hidden rounded-none px-5 py-4 text-left",
        "transition-colors duration-150",
        isDefault && "hover:bg-linear-to-r hover:from-amber-400/6 hover:to-transparent",
        isDanger && "hover:bg-linear-to-r hover:from-red-500/8 hover:to-transparent",
        isExit   && "hover:bg-white/2",
      )}
    >
      <span
        aria-hidden="true"
        className={cn(
          "absolute inset-y-0 left-0 w-0.5 origin-center scale-y-0 transition-transform duration-200 ease-out group-hover:scale-y-100",
          isDefault && "bg-amber-400/60",
          isDanger  && "bg-red-400/55",
          isExit    && "bg-white/15",
        )}
      />

      <span
        className={cn(
          "flex size-10 shrink-0 items-center justify-center rounded-xl transition-colors duration-150",
          isDefault && "bg-white/4 group-hover:bg-amber-400/8",
          isDanger  && "bg-red-500/6 group-hover:bg-red-500/10",
          isExit    && "bg-white/2 group-hover:bg-white/4",
        )}
      >
        <Icon
          className={cn(
            "size-5 transition-colors duration-150",
            isDefault && "text-white/28 group-hover:text-amber-400/80",
            isDanger  && "text-red-400/45 group-hover:text-red-300/70",
            isExit    && "text-white/15 group-hover:text-white/28",
          )}
        />
      </span>

      <span
        className={cn(
          "text-[14px] font-semibold tracking-[0.005em] transition-colors duration-150",
          isDefault && "text-white/48 group-hover:text-white/90",
          isDanger  && "text-red-400/65 group-hover:text-red-300",
          isExit    && "text-white/18 group-hover:text-white/42",
        )}
      >
        {item.label}
      </span>
    </Button>
  )
}
