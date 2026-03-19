import {
  BookOpen,
  Film,
  FilePlus2,
  FolderOpen,
  LogOut,
  PlaySquare,
  Scissors,
  X,
} from "lucide-react"
import type { LucideIcon } from "lucide-react"
import { sendEditorCommand } from "@/components/rockstar-editor/data"

export type NativeNavItem = {
  title: string
  subtitle: string
  path: string
  icon: LucideIcon
  accent?: "default" | "warning" | "danger"
  onClick?: () => void
  trackActive?: boolean
}

export const primaryNav: NativeNavItem[] = [
  {
    title: "Create New Project",
    subtitle: "Start a fresh montage workspace",
    path: "/editor/home",
    icon: FilePlus2,
    trackActive: true,
  },
  {
    title: "Load Project",
    subtitle: "Browse saved replay projects",
    path: "/editor/projects",
    icon: FolderOpen,
    onClick: () => sendEditorCommand("load_project"),
    trackActive: true,
  },
  {
    title: "Clip Management",
    subtitle: "Organize source clips",
    path: "/editor/clips",
    icon: Scissors,
    trackActive: true,
  },
  {
    title: "Video Gallery",
    subtitle: "Rendered exports and previews",
    path: "/editor/gallery",
    icon: PlaySquare,
    trackActive: true,
  },
  {
    title: "Tutorials",
    subtitle: "Editor guides and tips",
    path: "/editor/tutorials",
    icon: BookOpen,
    trackActive: true,
  },
  {
    title: "Director Mode",
    subtitle: "Cinematic free-camera tools",
    path: "/editor/director",
    icon: Film,
    accent: "warning",
    trackActive: true,
  },
]

export const systemNav: NativeNavItem[] = [
  {
    title: "Quit Game",
    subtitle: "Close GTA V",
    path: "/editor/home",
    icon: LogOut,
    accent: "danger",
    trackActive: false,
  },
  {
    title: "Exit Rockstar Editor",
    subtitle: "Return to gameplay",
    path: "/editor/home",
    icon: X,
    accent: "warning",
    trackActive: false,
  },
]
