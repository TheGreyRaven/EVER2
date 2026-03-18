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
  /** replay:/ URI of the clip thumbnail JPG */
  previewPath?: string
  /** Resolved disk path of the clip thumbnail JPG */
  previewDiskPath?: string
  /** Whether the thumbnail file exists on disk */
  previewExists?: boolean
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
  projects: ReplayProjectData[]
}
