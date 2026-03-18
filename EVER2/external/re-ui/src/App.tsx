import { MemoryRouter, Route, Routes } from "react-router-dom"

import { RockstarEditor } from "@/components/rockstar-editor"
import { useCefEvents } from "@/hooks/use-cef-events"
import { useViewportScale } from "@/hooks/use-viewport-scale"
import { ProjectEditorPage } from "@/pages/project-editor"

const AppInner = () => {
  const scale = useViewportScale()
  useCefEvents()

  return (
    //  bg-[#07090d] (set to transparent while testing)
    <div className="relative flex min-h-screen items-center justify-center overflow-hidden bg-transparent">

      <div
        aria-hidden="true"
        className="pointer-events-none absolute inset-0 flex items-center justify-center"
      >
        <div className="h-175 w-125 rounded-full bg-amber-500/2.5 blur-[120px]" />
      </div>

      <div
        className="relative z-10"
        style={{ transform: `scale(${scale})`, transformOrigin: "center center" }}
      >
        <Routes>
          <Route
            path="/"
            element={
              <div className="w-240">
                <RockstarEditor />
              </div>
            }
          />
          <Route path="/project" element={<ProjectEditorPage />} />
        </Routes>
      </div>

    </div>
  )
}

const App = () => (
  <MemoryRouter>
    <AppInner />
  </MemoryRouter>
)

export default App

