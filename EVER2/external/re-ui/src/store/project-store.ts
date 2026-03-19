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
  setPayload: (payload: ReplayProjectsPayload) => void
  selectProject: (project: ReplayProjectData) => void
  setLastCommandResponse: (response: EditorCommandResponse) => void
  setLoading: (loading: boolean) => void
  reset: () => void
}

export const useProjectStore = create<ProjectStore>((set) => ({
  payload: null,
  selectedProject: null,
  lastCommandResponse: null,
  isLoading: false,
  setPayload: (payload) =>
    set((state) => ({
      payload,
      isLoading: false,
      // Auto-select first project if nothing selected yet
      selectedProject:
        state.selectedProject == null && payload.projects.length > 0
          ? payload.projects[0]
          : state.selectedProject,
    })),
  selectProject: (selectedProject) => set({ selectedProject }),
  setLastCommandResponse: (lastCommandResponse) => set({ lastCommandResponse }),
  setLoading: (isLoading) => set({ isLoading }),
  reset: () =>
    set({
      payload: null,
      selectedProject: null,
      lastCommandResponse: null,
      isLoading: false,
    }),
}))
