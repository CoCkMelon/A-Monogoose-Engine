import {
  Timer, Layers, Radio, Volume2, MousePointer2, GitCompareArrows, Boxes, FileCog,
} from "lucide-react";
import { useRevealRoot } from "../hooks/useInView";

const MODULES = [
  {
    icon: FileCog,
    name: "web shim · web/app_em.c",
    c: "app_sdl.c owns threads on desktop",
    web: "app_em.c owns the rAF loop on web",
    note: "The engine's contract was built for this: the GAME implements app_init/app_event/app_fixed/app_resize/app_render; the ENGINE owns the platform bootstrap — 'SDL3 callback main on desktop, web shim on Emscripten' (app.h). We wrote exactly that shim; the games are untouched.",
  },
  {
    icon: Timer,
    name: "1000 Hz logic grid",
    c: "logic thread: app_fixed(0.001) per tick",
    web: "inline accumulator, bounded catch-up",
    note: "No pthreads without COOP/COEP headers, so the shim steps app_fixed(0.001) inside SDL_AppIterate off wall-clock debt — clamped at 0.12 s, mirroring logic_thread's 'fell behind: don't burst-catch-up' rule.",
  },
  {
    icon: Layers,
    name: "one-batch renderer",
    c: "one program, loaded via injected getter",
    web: "-sFULL_ES3=1 · WebGL2 ES 3.0 pipeline",
    note: "render.c dual-ships its shader bodies: '#version 330 core' on desktop, '#version 300 es' when the desc says GLES (the build flips that default for __EMSCRIPTEN__). The injected SDL_GL_GetProcAddress loader resolves on FULL_ES3 untouched.",
  },
  {
    icon: Volume2,
    name: "synth audio",
    c: "SPSC ring, SDL stream callback pulls",
    web: "pull-mode audio_render per frame",
    note: "The mixer never calls game logic from audio threads — so the shim mixes ~1024-frame chunks with audio_render() each iterate and feeds the SDL audio stream device. WebAudio autoplay policy is answered by a one-shot resume export.",
  },
  {
    icon: MousePointer2,
    name: "canvas isolation",
    c: "one window, one GL context",
    web: "per-instance SDL canvas selector",
    note: "Three WASM instances share one page. SDL's Emscripten backend binds '#canvas' by default, so each build compiles its own SDL_HINT_EMSCRIPTEN_CANVAS_SELECTOR — memory, raymarch and line_draw never fight over a context.",
  },
  {
    icon: Radio,
    name: "determinism",
    c: "xorshift32 ame_rand + seqlock snapshots",
    web: "verbatim in wasm — same seeds, same games",
    note: "Integer-only RNG and atomic snapshot publication survive compilation exactly: a seed that hides pairs on desktop hides the same pairs in the browser. Net code is stubbed to force the documented offline hot-seat path.",
  },
];

const DESKTOP_C = `/* src/app_sdl.c — desktop: split threads */
static int logic_thread(void *ud) {
  const double dt = 0.001;          /* 1000 Hz */
  Uint64 next = SDL_GetPerformanceCounter();
  while (g_run) {
    in_begin_step();
    if (app_fixed(dt) != 0)
      /* request shutdown */ break;
    next += one_ms;
    SDL_DelayPrecise(until(next)); /* sleep to tick */
  }
}

SDL_AppResult SDL_AppIterate(void *s) {
  app_render();                   /* latest snapshot */
  SDL_GL_SwapWindow(g_window);
}`;

const WEB_C = `/* web/app_em.c — browser: same hooks, one thread */
SDL_AppResult SDL_AppIterate(void *s) {
  double elapsed = wall_dt();     /* clamp 0.25 s */
  g_acc += elapsed;
  if (g_acc > 0.12) g_acc = 0.12; /* don't burst */
  while (g_acc >= 0.001) {
    in_begin_step();
    if (app_fixed(0.001f) != 0)
      return SDL_APP_SUCCESS;     /* quit */
    g_acc -= 0.001;
  }
  app_render();                   /* latest snapshot */
  audio_pump(elapsed);            /* pull mix */
  SDL_GL_SwapWindow(g_window);
}`;

export default function Architecture() {
  const ref = useRevealRoot<HTMLElement>();
  return (
    <section id="architecture" ref={ref} className="relative mx-auto max-w-6xl px-5 py-24 sm:px-8 sm:py-32">
      <div className="rv mb-3 flex items-center gap-3 font-mono text-[11px] tracking-[0.25em] text-phosphor">
        <span className="h-px w-8 bg-phosphor/60" />
        $ llvm-objdump -h public/wasm/memory_game.wasm
      </div>
      <h2 className="rv font-display text-4xl font-bold tracking-tight text-ink sm:text-5xl" style={{ ["--rv-delay" as string]: "60ms" }}>
        Compiled, not paraphrased.
      </h2>
      <p className="rv mt-5 max-w-2xl text-[15px] leading-relaxed text-dim" style={{ ["--rv-delay" as string]: "120ms" }}>
        Emscripten maps nearly everything an SDL3/GLES engine needs; what it can't provide —
        real threads, a desktop GL profile — costs exactly two deltas: a 200-line platform shim
        (already anticipated by the engine's own app.h) and a GLSL-flavor flag. Everything else
        ships to the browser byte-for-byte.
      </p>

      <div className="mt-14 grid gap-4 sm:grid-cols-2 lg:grid-cols-3">
        {MODULES.map((m, i) => (
          <div
            key={m.name}
            className="rv cabinet group rounded-xl p-5 transition-all duration-500 hover:-translate-y-1 hover:shadow-[0_20px_60px_rgba(0,0,0,0.5)]"
            style={{ ["--rv-delay" as string]: `${i * 60}ms` }}
          >
            <div className="flex items-center justify-between">
              <m.icon className="h-5 w-5 text-phosphor transition-transform duration-500 group-hover:scale-110" />
              <Boxes className="h-3.5 w-3.5 text-dim/40" />
            </div>
            <h3 className="mt-4 font-mono text-sm font-bold tracking-wide text-ink">{m.name}</h3>
            <div className="mt-2 space-y-1 font-mono text-[11px]">
              <div className="text-ice">c23&nbsp;&nbsp;· {m.c}</div>
              <div className="text-phosphor">wasm · {m.web}</div>
            </div>
            <p className="mt-3 text-[12.5px] leading-relaxed text-dim">{m.note}</p>
          </div>
        ))}
      </div>

      {/* desktop <-> web platform diff */}
      <div className="rv cabinet mt-12 overflow-hidden rounded-xl" style={{ ["--rv-delay" as string]: "100ms" }}>
        <div className="flex items-center gap-2.5 border-b border-hairline px-5 py-3.5 font-mono text-[11px] tracking-wide text-ink">
          <GitCompareArrows className="h-4 w-4 text-phosphor" />
          THE WHOLE PORT — how the 1000 Hz logic grid survives without threads
        </div>
        <div className="grid lg:grid-cols-2">
          <div className="border-b border-hairline lg:border-b-0 lg:border-r">
            <div className="border-b border-hairline bg-void/50 px-5 py-2 font-mono text-[10px] tracking-wider text-ice">
              desktop · SDL3 callbacks + SDL_CreateThread
            </div>
            <pre className="overflow-x-auto p-5 font-mono text-[11px] leading-[1.75] text-dim">
              {DESKTOP_C}
            </pre>
          </div>
          <div>
            <div className="border-b border-hairline bg-void/50 px-5 py-2 font-mono text-[10px] tracking-wider text-phosphor">
              emscripten · rAF iterate + wall-clock accumulator
            </div>
            <pre className="overflow-x-auto p-5 font-mono text-[11px] leading-[1.75] text-ink/80">
              {WEB_C}
            </pre>
          </div>
        </div>
      </div>
    </section>
  );
}
