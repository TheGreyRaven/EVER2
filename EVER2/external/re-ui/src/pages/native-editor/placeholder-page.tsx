import { AppWindow } from "lucide-react"

import { Card } from "@/components/ui/card"

type NativePlaceholderPageProps = {
  title: string
  description: string
}

export const NativePlaceholderPage = ({
  title,
  description,
}: NativePlaceholderPageProps) => {
  return (
    <div className="flex h-full p-6">
      <Card className="flex flex-1 items-center justify-center border-white/6 bg-black/22 p-8">
        <div className="max-w-lg text-center">
          <div className="mx-auto mb-4 flex size-14 items-center justify-center rounded-xl border border-white/8 bg-white/3">
            <AppWindow className="size-6 text-white/28" />
          </div>
          <h2 className="text-[24px] font-semibold tracking-[-0.02em] text-white/88">{title}</h2>
          <p className="mt-3 text-[12px] leading-relaxed text-white/42">{description}</p>
        </div>
      </Card>
    </div>
  )
}
