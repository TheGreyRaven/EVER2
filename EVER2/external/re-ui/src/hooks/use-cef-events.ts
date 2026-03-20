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
  const setMontageReady = useProjectStore((s) => s.setMontageReady)
  const setLastCommandResponse = useProjectStore(
    (s) => s.setLastCommandResponse
  )
  const appendClipToSelectedProject = useProjectStore(
    (s) => s.appendClipToSelectedProject
  )

  useEffect(() => {
    const handleCefData = (data: unknown) => {
      const payload = parseReplayProjectsPayload(data)
      if (payload) setPayload(payload)

      const response = parseEditorCommandResponse(data)
      if (response) {
        setLastCommandResponse(response)
        if (response.action === "load_project") {
          setLoading(false)
        }
      }

      if (
        data !== null &&
        typeof data === "object" &&
        (data as Record<string, unknown>)["event"] === "ever2_project_loaded"
      ) {
        setMontageReady(true)
      }

      if (
        data !== null &&
        typeof data === "object" &&
        (data as Record<string, unknown>)["event"] === "ever2_clip_added"
      ) {
        const d = data as Record<string, unknown>
        const baseName =
          typeof d.clipBaseName === "string" ? d.clipBaseName : ""
        const ownerIdText =
          d.clipOwnerId !== undefined ? String(d.clipOwnerId) : "0"
        const previewDiskPath =
          typeof d.clipPreviewDiskPath === "string" ? d.clipPreviewDiskPath : ""
        const previewExists =
          typeof d.clipPreviewExists === "boolean" ? d.clipPreviewExists : false
        appendClipToSelectedProject(
          baseName,
          ownerIdText,
          previewDiskPath,
          previewExists
        )
      }
    }

    const handleCefEvent = (event: Event) => {
      handleCefData((event as CustomEvent<unknown>).detail)
    }

    const handleWindowMessage = (event: MessageEvent<unknown>) => {
      handleCefData(event.data)
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
  }, [
    appendClipToSelectedProject,
    setLastCommandResponse,
    setLoading,
    setMontageReady,
    setPayload,
  ])
}
