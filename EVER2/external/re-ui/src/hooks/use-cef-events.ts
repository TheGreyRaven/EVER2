import { useEffect } from "react"

import {
  parseEditorCommandResponse,
  parseReplayProjectsPayload,
} from "@/components/rockstar-editor/data"
import { useProjectStore } from "@/store/project-store"

/**
 * Global CEF / window.postMessage event listener.
 * Mount this once at the top of the component tree so that project data
 * is available regardless of which page is currently rendered.
 */
export const useCefEvents = () => {
  const setPayload = useProjectStore((s) => s.setPayload)
  const setLoading = useProjectStore((s) => s.setLoading)
  const setLastCommandResponse = useProjectStore(
    (s) => s.setLastCommandResponse
  )

  useEffect(() => {
    const handleCefEvent = (event: Event) => {
      const custom = event as CustomEvent<unknown>
      const payload = parseReplayProjectsPayload(custom.detail)
      if (payload) setPayload(payload)

      const response = parseEditorCommandResponse(custom.detail)
      if (response) {
        setLastCommandResponse(response)
        if (response.action === "load_project") {
          setLoading(false)
        }
      }
    }

    const handleWindowMessage = (event: MessageEvent<unknown>) => {
      const payload = parseReplayProjectsPayload(event.data)
      if (payload) setPayload(payload)

      const response = parseEditorCommandResponse(event.data)
      if (response) {
        setLastCommandResponse(response)
        if (response.action === "load_project") {
          setLoading(false)
        }
      }
    }

    window.addEventListener("ever:cef:message", handleCefEvent as EventListener)
    window.addEventListener("message", handleWindowMessage)

    return () => {
      window.removeEventListener(
        "ever:cef:message",
        handleCefEvent as EventListener
      )
      window.removeEventListener("message", handleWindowMessage)
    }
  }, [setLastCommandResponse, setLoading, setPayload])
}
