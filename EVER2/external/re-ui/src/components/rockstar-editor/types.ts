import type { LucideIcon } from "lucide-react"

export type MenuItemVariant = "default" | "danger" | "exit"

export type MenuItemConfig = {
  icon: LucideIcon
  label: string
  description?: string
  variant?: MenuItemVariant
  onClick?: () => void
}
