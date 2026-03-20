import { create } from "zustand"

import type {
  EditorCommandResponse,
  ReplayProjectData,
  ReplayProjectsPayload,
} from "@/components/rockstar-editor/types"

type ProjectStore = {
  payload: ReplayProjectsPayload | null
  selectedProject: ReplayProjectData | null
  lastCommandResponse: EditorCommandResponse | null
  isLoading: boolean
  montageReady: boolean
  setPayload: (payload: ReplayProjectsPayload) => void
  selectProject: (project: ReplayProjectData) => void
  setLastCommandResponse: (response: EditorCommandResponse) => void
  setLoading: (loading: boolean) => void
  setMontageReady: (ready: boolean) => void
  appendClipToSelectedProject: (
    baseName: string,
    ownerIdText: string,
    previewDiskPath?: string,
    previewExists?: boolean
  ) => void
  reset: () => void
}

export const useProjectStore = create<ProjectStore>((set) => ({
  payload: null,
  selectedProject: null,
  lastCommandResponse: null,
  isLoading: false,
  montageReady: false,
  setPayload: (payload) =>
    set((state) => ({
      payload,
      isLoading: false,
      montageReady: false,
      // Preserve selection by index across payload refreshes and always rebind to the latest object.
      selectedProject:
        payload.projects.length === 0
          ? null
          : state.selectedProject == null
            ? payload.projects[0]
            : (payload.projects.find(
                (project) => project.index === state.selectedProject?.index
              ) ?? payload.projects[0]),
    })),
  selectProject: (selectedProject) => set({ selectedProject }),
  setLastCommandResponse: (lastCommandResponse) => set({ lastCommandResponse }),
  setLoading: (isLoading) => set({ isLoading }),
  setMontageReady: (montageReady) => set({ montageReady }),
  appendClipToSelectedProject: (
    baseName,
    ownerIdText,
    previewDiskPath,
    previewExists
  ) =>
    set((state) => {
      if (!state.selectedProject) return state
      const newIndex = state.selectedProject.clips.length
      const updatedClips = [
        ...state.selectedProject.clips,
        {
          index: newIndex,
          baseName,
          ownerIdText,
          previewDiskPath: previewDiskPath ?? "",
          previewExists: previewExists ?? false,
        },
      ]
      const updatedProject = {
        ...state.selectedProject,
        clips: updatedClips,
        clipCount: updatedClips.length,
      }
      const updatedPayload = state.payload
        ? {
            ...state.payload,
            projects: state.payload.projects.map((p) =>
              p.index === state.selectedProject!.index ? updatedProject : p
            ),
          }
        : state.payload
      return {
        selectedProject: updatedProject,
        payload: updatedPayload,
      }
    }),
  reset: () =>
    set({
      payload: null,
      selectedProject: null,
      lastCommandResponse: null,
      isLoading: false,
      montageReady: false,
    }),
}))
