import { useEffect, useRef } from "react";
import { ArrowDown, GitBranch, Play, TerminalSquare } from "lucide-react";
import { sfx } from "../lib/sfx";

/* Cursor trail — homage to the master branch's flagship curve_paint example:
 * mouse motion appends vertices to a G(H)L line-strip; here, glowing canvas. */
function useTrail(ref: React.RefObject<HTMLCanvasElement | null>) {
  useEffect(() => {
    const cv = ref.current;
    if (!cv) return;
    const ctx = cv.getContext("2d");
    if (!ctx) return;
    let w = 0;
    let h = 0;
    let raf = 0;
    const dpr = Math.min(2, window.devicePixelRatio || 1);

    const pts: { x: number; y: number; t: number }[] = [];
    let last = performance.now();
    let t0 = last;
    let mouseSeen = false;

    const resize = () => {
      w = cv.clientWidth;
      h = cv.clientHeight;
      cv.width = w * dpr;
      cv.height = h * dpr;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    };
    resize();
    window.addEventListener("resize", resize);

    const onMove = (e: PointerEvent) => {
      const r = cv.getBoundingClientRect();
      const x = e.clientX - r.left;
      const y = e.clientY - r.top;
      if (x < 0 || y < 0 || x > r.width || y > r.height) return;
      mouseSeen = true;
      pts.push({ x, y, t: performance.now() });
    };
    window.addEventListener("pointermove", onMove, { passive: true });

    const draw = (now: number) => {
      raf = requestAnimationFrame(draw);
      const dt = Math.min(0.05, (now - last) / 1000);
      last = now;

      /* idle: a phantom mongoose draws lissajous curves */
      if (!mouseSeen || now - (pts[pts.length - 1]?.t ?? 0) > 2600) {
        const tt = (now - t0) / 1000;
        const cx = w * 0.5 + Math.sin(tt * 0.45) * w * 0.33;
        const cy = h * 0.52 + Math.sin(tt * 0.9 + 1.3) * h * 0.22;
        pts.push({ x: cx, y: cy, t: now });
      }

      while (pts.length && now - pts[0].t > 1400) pts.shift();

      ctx.clearRect(0, 0, w, h);
      ctx.globalCompositeOperation = "lighter";
      ctx.lineCap = "round";
      ctx.lineJoin = "round";
      for (let i = 1; i < pts.length; i++) {
        const a = pts[i - 1];
        const b = pts[i];
        const age = 1 - (now - b.t) / 1400;
        if (age <= 0) continue;
        const alpha = Math.pow(age, 1.6);
        ctx.strokeStyle = `rgba(51,255,128,${0.5 * alpha})`;
        ctx.lineWidth = 10 * age;
        ctx.beginPath();
        ctx.moveTo(a.x, a.y);
        ctx.lineTo(b.x, b.y);
        ctx.stroke();
        ctx.strokeStyle = `rgba(220,255,235,${0.85 * alpha})`;
        ctx.lineWidth = 1.6 * age;
        ctx.beginPath();
        ctx.moveTo(a.x, a.y);
        ctx.lineTo(b.x, b.y);
        ctx.stroke();
      }
      void dt;
    };
    raf = requestAnimationFrame(draw);
    return () => {
      cancelAnimationFrame(raf);
      window.removeEventListener("resize", resize);
      window.removeEventListener("pointermove", onMove);
    };
  }, [ref]);
}

export default function Hero() {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  useTrail(canvasRef);

  return (
    <header className="relative flex min-h-[100svh] flex-col overflow-hidden grid-bg">
      {/* ambient glows */}
      <div className="pointer-events-none absolute -top-40 left-1/2 h-[560px] w-[900px] -translate-x-1/2 rounded-full bg-[radial-gradient(closest-side,rgba(51,255,128,0.09),transparent)]" />
      <div className="pointer-events-none absolute bottom-0 right-[-10%] h-[420px] w-[620px] rounded-full bg-[radial-gradient(closest-side,rgba(255,51,204,0.05),transparent)]" />

      <canvas ref={canvasRef} className="absolute inset-0 h-full w-full" />

      <div className="relative z-10 mx-auto flex w-full max-w-6xl flex-1 flex-col justify-center px-5 pt-28 sm:px-8">
        <div className="mb-6 flex flex-wrap items-center gap-2 font-mono text-[11px] tracking-wide text-dim">
          <span className="flex items-center gap-2 rounded-full border border-hairline bg-panel/70 px-3 py-1.5">
            <GitBranch className="h-3.5 w-3.5 text-phosphor" />
            agent/ame-next-20260905-4ad0fd06
          </span>
          <span className="flex items-center gap-2 rounded-full border border-hairline bg-panel/70 px-3 py-1.5">
            <TerminalSquare className="h-3.5 w-3.5 text-amber" />
            C23 → wasm32 · Emscripten · WebGL2
          </span>
        </div>

        <h1 className="select-none font-display text-[clamp(3rem,9.5vw,7.5rem)] font-bold leading-[0.92] tracking-tight">
          <span className="block text-ink">A MONGOOSE</span>
          <span className="stroke-text block">ENGINE</span>
          <span className="block text-phosphor glow-green">//ON THE WEB</span>
        </h1>

        <p className="mt-7 max-w-xl text-[15px] leading-relaxed text-dim sm:text-base">
          All branches of <span className="font-mono text-[13px] text-ink">CoCkMelon/A-Monogoose-Engine</span> reviewed.
          The most web-suited one — the C23 rewrite, <span className="text-ink">ame-next</span> — is compiled
          below with Emscripten: the engine's own C sources run as WebAssembly,
          with its competitive Memory anchor game, raymarch runner, and line drawing playable for real.
        </p>

        <div className="mt-9 flex flex-wrap items-center gap-4">
          <a
            href="#arcade"
            onClick={() => sfx.unlock()}
            className="group flex items-center gap-2.5 rounded-md bg-phosphor px-6 py-3.5 font-mono text-[13px] font-bold tracking-wide text-void transition-all hover:shadow-[0_0_36px_rgba(51,255,128,0.4)]"
          >
            <Play className="h-4 w-4 transition-transform group-hover:scale-125" />
            RUN THE BUILD
          </a>
          <a
            href="#branches"
            className="flex items-center gap-2.5 rounded-md border border-hairline bg-panel/60 px-6 py-3.5 font-mono text-[13px] tracking-wide text-ink transition-colors hover:border-phosphor/50 hover:text-phosphor"
          >
            <GitBranch className="h-4 w-4" />
            git log --all --review
          </a>
        </div>
      </div>

      <div className="relative z-10 mx-auto flex w-full max-w-6xl items-end justify-between px-5 pb-8 sm:px-8">
        <div className="font-mono text-[10px] leading-relaxed text-dim/70">
          curve_paint.exe — move your cursor
          <br />
          GL_LINE_STRIP · vertices appended in realtime
        </div>
        <a href="#branches" className="flex flex-col items-center gap-2 text-dim transition-colors hover:text-phosphor">
          <span className="font-mono text-[10px] tracking-[0.3em]">SCROLL</span>
          <ArrowDown className="h-4 w-4 animate-bounce" />
        </a>
      </div>
    </header>
  );
}
