import { useState } from "react";
import { Grid2x2, Orbit, Spline, FileCode2, Braces, Binary } from "lucide-react";
import { useRevealRoot } from "../hooks/useInView";
import { sfx } from "../lib/sfx";
import WasmGame from "./WasmGame";
import memoryJs from "../wasm/memory_game.js?raw";
import raymarchJs from "../wasm/raymarch.js?raw";
import lineJs from "../wasm/line_draw.js?raw";

const TABS = [
  {
    id: "memory",
    exe: "memory_game.wasm",
    icon: Grid2x2,
    title: "COMPETITIVE MEMORY",
    src: "agent branch · examples/memory_game — compiled C23→wasm32",
    rawJs: memoryJs,
    factory: "AmeMemory" as const,
    canvasId: "ame_canvas_mem",
    blurb:
      "The anchor game, compiled from the actual C23. 4×4 pairs on a shadow-mapped 3D table, strict two-player alternation — two opens per turn, turn passes on every resolve. Hot-seat: share the mouse. Click after WIN for the voted rematch. Rules proven in Lean 4 upstream; the synth voices come from the engine's own mixer.",
    tags: ["955 kB wasm", "3D one-batch + shadow maps", "baked font atlas", "hot-seat 2P"],
  },
  {
    id: "raymarch",
    exe: "raymarch_arcade.wasm",
    icon: Orbit,
    title: "RAYMARCH ARCADE",
    src: "agent branch · examples/raymarch_arcade — compiled C23→wasm32",
    rawJs: raymarchJs,
    factory: "AmeRm" as const,
    canvasId: "ame_canvas_rm",
    blurb:
      "Dodge falling orbs for 45 s; each hit costs 15% health. The game shades its 2D SDF scene on the CPU into a 480×270 buffer and uploads it through the engine's ONE-batch renderer — no second pipeline anywhere. Move with A/D or ←/→, quit with Q. Verbatim master-branch constants.",
    tags: ["752 kB wasm", "CPU-shaded SDF", "45 s gauntlet", "A/D · arrows"],
  },
  {
    id: "line",
    exe: "line_draw.wasm",
    icon: Spline,
    title: "LINE DRAW",
    src: "agent branch · examples/line_draw — compiled C23→wasm32",
    rawJs: lineJs,
    factory: "AmeLine" as const,
    canvasId: "ame_canvas_line",
    blurb:
      "The engine's first hello-world: hold the left button and move. Pointer motion is accumulated and appended as vertices to a growing line strip, drawn through the same batch as everything else. The demo that started the whole engine — running as the exact C source intended.",
    tags: ["748 kB wasm", "vertex-append strip", "SDL pointer events", "the original demo"],
  },
] as const;

type TabId = (typeof TABS)[number]["id"];

export default function Arcade() {
  const ref = useRevealRoot<HTMLElement>();
  const [tab, setTab] = useState<TabId>("memory");
  const [mounted, setMounted] = useState<Set<TabId>>(new Set(["memory"]));

  const activate = (id: TabId) => {
    setTab(id);
    setMounted((m) => (m.has(id) ? m : new Set(m).add(id)));
    sfx.unlock();
    sfx.ui();
  };

  const current = TABS.find((t) => t.id === tab)!;

  return (
    <section id="arcade" ref={ref} className="relative mx-auto max-w-6xl px-5 py-24 sm:px-8 sm:py-32">
      <div className="rv mb-3 flex items-center gap-3 font-mono text-[11px] tracking-[0.25em] text-phosphor">
        <span className="h-px w-8 bg-phosphor/60" />
        $ emcc -O2 --use-port=sdl3 -sFULL_ES3=1 → a.out.wasm
      </div>
      <h2 className="rv font-display text-4xl font-bold tracking-tight text-ink sm:text-5xl" style={{ ["--rv-delay" as string]: "60ms" }}>
        The engine itself, <span className="text-phosphor glow-green">compiled and running.</span>
      </h2>
      <p className="rv mt-5 max-w-2xl text-[15px] leading-relaxed text-dim" style={{ ["--rv-delay" as string]: "120ms" }}>
        These are not reimplementations. Each cabinet below instantiates a WebAssembly binary
        built from the branch's own C23 sources — SDL3 windowing onto a WebGL2 context, the
        injected GL loader on FULL_ES3, and the 1 kHz logic grid driven by a wall-clock
        accumulator inside the browser's animation loop.
      </p>

      {/* cabinet */}
      <div className="rv mt-12" style={{ ["--rv-delay" as string]: "160ms" }}>
        <div className="cabinet crt-mask overflow-hidden rounded-2xl border border-hairline">
          {/* title bar */}
          <div className="relative z-10 flex items-center justify-between border-b border-hairline bg-void/70 px-4 py-2.5 sm:px-5">
            <div className="flex items-center gap-3">
              <div className="flex gap-1.5">
                <span className="h-2.5 w-2.5 rounded-full bg-ember/70" />
                <span className="h-2.5 w-2.5 rounded-full bg-amber/70" />
                <span className="h-2.5 w-2.5 rounded-full bg-phosphor/70" />
              </div>
              <span className="font-mono text-[11px] text-dim">
                <span className="text-phosphor">wasm32-emscripten</span> · {current.exe}
              </span>
            </div>
            <div className="flex items-center gap-3 font-mono text-[9px] tracking-wider text-dim/70">
              <span className="hidden items-center gap-1.5 sm:flex">
                <Binary className="h-3 w-3" />
                C23 · SDL3 · GLES3 (300 es)
              </span>
              <span className="flex items-center gap-1.5 text-phosphor">
                <span className="inline-block h-1.5 w-1.5 rounded-full bg-phosphor pulse-dot" />
                NATIVE SPEED
              </span>
            </div>
          </div>

          {/* tabs */}
          <div role="tablist" className="relative z-10 flex border-b border-hairline bg-carbon/80">
            {TABS.map((t) => {
              const on = tab === t.id;
              return (
                <button
                  key={t.id}
                  role="tab"
                  aria-selected={on}
                  onClick={() => activate(t.id)}
                  className={`relative flex flex-1 items-center justify-center gap-2 px-2 py-3 font-mono text-[10px] tracking-wide transition-colors sm:text-[11px] ${
                    on ? "text-phosphor" : "text-dim hover:text-ink"
                  }`}
                >
                  <t.icon className="h-3.5 w-3.5" />
                  <span className="hidden sm:inline">{t.exe}</span>
                  <span className="sm:hidden">{t.title.split(" ")[0]}</span>
                  {on && <span className="absolute inset-x-0 bottom-0 h-0.5 bg-phosphor shadow-[0_0_12px_rgba(51,255,128,0.8)]" />}
                </button>
              );
            })}
          </div>

          {/* wasm viewport */}
          <div className="relative h-[480px] bg-black sm:h-[620px]">
            {TABS.map(
              (t) =>
                mounted.has(t.id) && (
                  <div key={t.id} className={tab === t.id ? "h-full" : "hidden h-full"}>
                    <WasmGame rawJs={t.rawJs} factory={t.factory} canvasId={t.canvasId} active={tab === t.id} />
                  </div>
                )
            )}
          </div>
        </div>

        {/* per-game info */}
        <div className="mt-5 grid gap-4 md:grid-cols-[1fr_auto]">
          <div>
            <div className="flex flex-wrap items-center gap-3">
              <h3 className="font-display text-xl font-bold text-ink">{current.title}</h3>
              <span className="flex items-center gap-1.5 rounded border border-hairline bg-panel/60 px-2 py-1 font-mono text-[9.5px] text-ice">
                <FileCode2 className="h-3 w-3" />
                {current.src}
              </span>
            </div>
            <p className="mt-2 max-w-3xl text-[13.5px] leading-relaxed text-dim">{current.blurb}</p>
          </div>
          <div className="flex flex-wrap items-start gap-1.5 md:max-w-xs md:justify-end">
            {current.tags.map((t) => (
              <span key={t} className="flex items-center gap-1 rounded-full border border-hairline px-2.5 py-1 font-mono text-[9.5px] text-dim">
                <Braces className="h-2.5 w-2.5 text-phosphor/70" />
                {t}
              </span>
            ))}
          </div>
        </div>
      </div>
    </section>
  );
}
