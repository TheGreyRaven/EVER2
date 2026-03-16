import { Separator as ShadcnSeparator } from "@/components/ui/separator"

// Thin wrapper around the Shadcn Separator with project-specific styling.
// Keeping the same public API so index.tsx doesn't need to change.
export const Separator = () => (
  <ShadcnSeparator className="mx-5 bg-white/5.5" />
)
