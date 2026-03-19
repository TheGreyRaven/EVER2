import { MemoryRouter, Navigate, Route, Routes } from "react-router-dom"

import { TooltipProvider } from "@/components/ui/tooltip"
import { useCefEvents } from "@/hooks/use-cef-events"
import { useViewportScale } from "@/hooks/use-viewport-scale"
import { NativeEditorHomePage } from "@/pages/native-editor/home-page"
import { NativeEditorLayout } from "@/pages/native-editor/layout"
import { NativeMontagePage } from "@/pages/native-editor/montage-page"
import { NativePlaceholderPage } from "@/pages/native-editor/placeholder-page"
import { NativeProjectsPage } from "@/pages/native-editor/projects-page"

const AppInner = () => {
  const scale = useViewportScale()
  useCefEvents()

  return (
    <div className="relative flex min-h-screen items-center justify-center overflow-hidden bg-transparent">
      <div
        aria-hidden="true"
        className="pointer-events-none absolute inset-0 flex items-center justify-center"
      >
        <div className="h-185 w-140 rounded-full bg-amber-400/4 blur-[130px]" />
      </div>

      <div
        className="relative z-10"
        style={{ transform: `scale(${scale})`, transformOrigin: "center center" }}
      >
        <Routes>
          <Route path="/" element={<Navigate to="/editor/home" replace />} />

          <Route path="/editor" element={<NativeEditorLayout />}>
            <Route path="home" element={<NativeEditorHomePage />} />
            <Route path="projects" element={<NativeProjectsPage />} />
            <Route
              path="clips"
              element={
                <NativePlaceholderPage
                  title="Clip Management"
                  description="Placeholder screen for clip browser, clip metadata, and clip-level actions such as trim, rename, mark-delete, and preview playback."
                />
              }
            />
            <Route
              path="gallery"
              element={
                <NativePlaceholderPage
                  title="Video Gallery"
                  description="Placeholder screen for rendered videos and export history. This is where playback, upload, and storage management panels can be integrated."
                />
              }
            />
            <Route
              path="tutorials"
              element={
                <NativePlaceholderPage
                  title="Tutorials"
                  description="Placeholder screen for onboarding and advanced editor tips. You can map this to embedded docs or a native web tutorial feed."
                />
              }
            />
            <Route
              path="director"
              element={
                <NativePlaceholderPage
                  title="Director Mode"
                  description="Placeholder launchpad for Director Mode integration. Final implementation can invoke native launch flow and display session status."
                />
              }
            />
          </Route>

          <Route path="/editor/montage" element={<NativeMontagePage />} />

          <Route path="*" element={<Navigate to="/editor/home" replace />} />
        </Routes>
      </div>
    </div>
  )
}

const App = () => (
  <TooltipProvider>
    <MemoryRouter>
      <AppInner />
    </MemoryRouter>
  </TooltipProvider>
)

export default App

