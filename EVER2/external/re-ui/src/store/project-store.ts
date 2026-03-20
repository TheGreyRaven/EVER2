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
  reset: () =>
    set({
      payload: null,
      selectedProject: null,
      lastCommandResponse: null,
      isLoading: false,
    }),
}))
