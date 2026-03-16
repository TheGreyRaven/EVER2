import { RockstarEditor } from "@/components/rockstar-editor"
import { useViewportScale } from "@/hooks/use-viewport-scale"

const App = () => {
  const scale = useViewportScale()

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
        className="relative z-10 w-240"
        style={{ transform: `scale(${scale})`, transformOrigin: "center center" }}
      >
        <RockstarEditor />
      </div>

    </div>
  )
}

export default App

