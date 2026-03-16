import {
  BookOpen,
  Clapperboard,
  Film,
  FilePlus2,
  FolderOpen,
  LogOut,
  PlaySquare,
  X,
} from "lucide-react"

import type { MenuItemConfig } from "./types"

declare global {
  interface Window {
    everSendCefMessageToHost?: (payload: string) => boolean
  }
}

const sendEditorCommand = (
  action: "quit_game" | "exit_rockstar_editor" | "load_project",
) => {
  const payload = JSON.stringify({
    action,
    requestId: `${Date.now()}-${Math.random().toString(16).slice(2)}`,
  })

  try {
    if (typeof window.everSendCefMessageToHost === "function") {
      window.everSendCefMessageToHost(payload)
      return
    }

    if (window.parent && window.parent !== window) {
      window.parent.postMessage({ __everSendCefMessage: payload }, "*")
      return
    }

    console.warn("[EVER2 UI] Native bridge is unavailable for action:", action)
  } catch (error) {
    console.error("[EVER2 UI] Failed to send editor command:", action, error)
  }
}

export const primaryItems: MenuItemConfig[] = [
  {
    icon: FilePlus2,
    label: "Create new project",
    description:
      "Start a fresh cinematic project and begin organising your recorded clips.",
  },
  {
    icon: FolderOpen,
    label: "Load project",
    description:
      "Open an existing saved project and continue editing where you left off.",
    onClick: () => sendEditorCommand("load_project"),
  },
  {
    icon: Clapperboard,
    label: "Director mode",
    description:
      "Freely control cameras, angles, and scene direction in real time.",
  },
  {
    icon: Film,
    label: "Clip management",
    description:
      "Browse, trim, rename, and organise all of your recorded video clips.",
  },
  {
    icon: PlaySquare,
    label: "Video gallery",
    description:
      "Preview your completed renders and export them to your hard drive.",
  },
  {
    icon: BookOpen,
    label: "Tutorials",
    description:
      "Step-by-step guides covering the basics and advanced editor techniques.",
  },
]

export const exitItems: MenuItemConfig[] = [
  {
    icon: LogOut,
    label: "Quit game",
    variant: "danger",
    onClick: () => sendEditorCommand("quit_game"),
  },
  {
    icon: X,
    label: "Exit Rockstar Editor",
    variant: "exit",
    onClick: () => sendEditorCommand("exit_rockstar_editor"),
  },
]
