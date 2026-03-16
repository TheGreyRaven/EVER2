export const EditorHeader = () => {
  return (
    <header className="relative px-8 pb-7 pt-10 text-center">
      <div
        aria-hidden="true"
        className="pointer-events-none absolute inset-0 bg-linear-to-b from-amber-400/4 to-transparent"
      />

      <h1 className="relative text-[44px] font-bold leading-none tracking-[-0.035em] text-white">
        EVER<span className="text-[#ffba00]">2</span>
      </h1>

      <p className="relative mt-3 text-[11px] font-semibold uppercase tracking-[0.25em] text-white/30">
        Extended Video Export Revived
      </p>
    </header>
  )
}
