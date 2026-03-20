import type { LucideIcon } from "lucide-react"

export type MenuItemVariant = "default" | "danger" | "exit"

export type MenuItemConfig = {
  icon: LucideIcon
  label: string
  description?: string
  variant?: MenuItemVariant
  /** If set, clicking will also navigate to this in-app route */
  route?: string
  onClick?: () => void
}

export type ProjectClipData = {
  index: number
  baseName?: string
  path?: string
  diskPath?: string
  exists?: boolean
  uid?: number
  ownerId?: number
  ownerIdText?: string
  durationMs?: number
  favourite?: boolean
  modded?: boolean
  corrupt?: boolean
  /** replay:/ URI of the clip thumbnail JPG */
  previewPath?: string
  /** Resolved disk path of the clip thumbnail JPG */
  previewDiskPath?: string
  /** Whether the thumbnail file exists on disk */
  previewExists?: boolean
}

export type AvailableClipData = {
  uid: number
  sourceIndex?: number
  baseName?: string
  path?: string
  exists?: boolean
  ownerId?: number
  ownerIdText?: string
  durationMs?: number
  favourite?: boolean
  modded?: boolean
  corrupt?: boolean
}

export type ReplayProjectData = {
  index: number
  projectName: string
  path: string
  durationMs: number
  corrupt: boolean
  fileHash: number
  sizeBytes: number
  userId: number
  lastWriteRaw: number
  lastWriteLocal: string
  markDelete: boolean
  previewCandidate: string
  previewDiskPath: string
  previewExists: boolean
  clipArrayPtr: number
  clipCount16: number
  clipCap16: number
  clipBaseNameCount: number
  clipCount: number
  clips: ProjectClipData[]
}

export type ReplayProjectsPayload = {
  event: "ever2_load_project_data"
  status: "ready"
  requestId?: string
  projectCount: number
  availableClips?: AvailableClipData[]
  projects: ReplayProjectData[]
}

export type EditorCommandAction =
  | "quit_game"
  | "exit_rockstar_editor"
  | "load_project"
  | "add_clip_to_project"
  | "save_project"

export type EditorCommandResponse = {
  event: "ever2_command_response"
  action: string
  status: "accepted" | "error"
  requestId?: string
  message?: string
  sourceClipIndex?: number
  destinationIndex?: number
  projectCount?: number
}
