import { Check, X, GitCommitHorizontal, FlaskConical, Gamepad2, FileCode2, Trophy, ShieldCheck } from "lucide-react";
import { useRevealRoot } from "../hooks/useInView";

interface Branch {
  name: string;
  short: string;
  color: string;
  desc: string;
  stats: { icon: React.ReactNode; k: string; v: string }[];
  stack: string;
  fit: { label: string; ok: boolean }[];
  score: number; // web-fit percentage
  verdict: string;
  selected?: boolean;
}

const BRANCHES: Branch[] = [
  {
    name: "master",
    short: "fc7cdf3",
    color: "#7d968a",
    desc: "The original scaffold. SDL3 windowing + OpenGL 4.5 core, libasyncinput reading /dev/input/event*, flecs ECS, Box2D bridge, a Unity-like C++ API layer and 26 example dirs — curve painting, raymarch arcade, kenney platformer tiles.",
    stats: [
      { icon: <GitCommitHorizontal className="h-3.5 w-3.5" />, k: "commits", v: "97" },
      { icon: <FileCode2 className="h-3.5 w-3.5" />, k: "C/C++ lines", v: "25,020" },
      { icon: <Gamepad2 className="h-3.5 w-3.5" />, k: "example dirs", v: "26" },
      { icon: <FlaskConical className="h-3.5 w-3.5" />, k: "test suites", v: "0" },
    ],
    stack: "SDL3 · GL 4.5 core · evdev · flecs · Box2D",
    fit: [
      { label: "OpenGL 4.5 core — beyond WebGL2 (ES 3.0)", ok: false },
      { label: "Input requires /dev/input permissions", ok: false },
      { label: "Richest raw idea pool (raymarch SDF game)", ok: true },
    ],
    score: 24,
    verdict: "unportable as-is — desktop GL + kernel input",
  },
  {
    name: "ame-next-f2c36ea03349a2ca",
    short: "cfe5132",
    color: "#57d7ff",
    desc: "The C23 rebirth — \u201ca mongoose-shaped tree\u201d. Single core, dimension-macro builds, pool + generation handles, uniform-grid broadphase, seqlock snapshots, 19 ctest suites. The spec moved into docs/ and the spec decides.",
    stats: [
      { icon: <GitCommitHorizontal className="h-3.5 w-3.5" />, k: "commits", v: "100" },
      { icon: <FileCode2 className="h-3.5 w-3.5" />, k: "C/C++ lines", v: "10,667" },
      { icon: <Gamepad2 className="h-3.5 w-3.5" />, k: "example dirs", v: "2" },
      { icon: <FlaskConical className="h-3.5 w-3.5" />, k: "test suites", v: "19" },
    ],
    stack: "C23 · SDL3 · EGL/GLES · one-batch renderer",
    fit: [
      { label: "Clean architecture — ideal port substrate", ok: true },
      { label: "GLES-class renderer matches WebGL2", ok: true },
      { label: "Mid-rewrite: only 2 playable examples", ok: false },
    ],
    score: 58,
    verdict: "right bones, too early — anchor game unfinished",
  },
  {
    name: "agent/ame-next-20260905-4ad0fd06",
    short: "a6537f2",
    color: "#33ff80",
    desc: "The long agent session that finished the job: competitive Memory anchor game (server-authoritative online), Stage-2 rendering (forward lighting, offscreen + post pass, particles, shadow maps), opus decode, tilemap + text editor + raymarch ports, 17 suites green — plus a Lean 4 formal model of the rules.",
    stats: [
      { icon: <GitCommitHorizontal className="h-3.5 w-3.5" />, k: "commits", v: "26" },
      { icon: <FileCode2 className="h-3.5 w-3.5" />, k: "C lines", v: "47,120" },
      { icon: <Gamepad2 className="h-3.5 w-3.5" />, k: "example dirs", v: "9" },
      { icon: <FlaskConical className="h-3.5 w-3.5" />, k: "test suites", v: "17" },
    ],
    stack: "C23 · EGL/GLES one-batch · Lean 4 · TCP net · Opus",
    fit: [
      { label: "Fixed-step deterministic sim → rAF port 1:1", ok: true },
      { label: "One program, one batch → one WebGL2 pipeline", ok: true },
      { label: "Synth-voice SFX → WebAudio oscillators", ok: true },
      { label: "Thin-client net model → browser by design", ok: true },
    ],
    score: 96,
    verdict: "selected — every layer maps to a web API",
    selected: true,
  },
];

const MODULE_MAP: { c: string; web: string }[] = [
  { c: "logic thread · 1000 Hz fixed step", web: "inline 1 ms accumulator in rAF (app_em.c)" },
  { c: "SDL3 + EGL/GLES windowing", web: "--use-port=sdl3 → WebGL2 context" },
  { c: "injected GL proc loader", web: "-sFULL_ES3=1 keeps glX portable" },
  { c: "#version 330 core / 300 es dual", web: "gles desc → 300 es on web" },
  { c: "SPSC synth mixer + SDL stream", web: "pull-mode audio_render → WebAudio" },
  { c: "xorshift32 ame_rand", web: "runs verbatim in wasm (same shuffles)" },
];

function Meter({ score, color }: { score: number; color: string }) {
  return (
    <div>
      <div className="mb-1.5 flex items-center justify-between font-mono text-[10px] tracking-wide text-dim">
        <span>WEB-FIT</span>
        <span style={{ color }}>{score}%</span>
      </div>
      <div className="h-1.5 overflow-hidden rounded-full bg-white/5">
        <div
          className="h-full rounded-full transition-all duration-1000"
          style={{ width: `${score}%`, background: color, boxShadow: `0 0 12px ${color}88` }}
        />
      </div>
    </div>
  );
}

export default function BranchReview() {
  const ref = useRevealRoot<HTMLElement>();
  return (
    <section id="branches" ref={ref} className="relative mx-auto max-w-6xl px-5 py-24 sm:px-8 sm:py-32">
      <div className="rv mb-3 flex items-center gap-3 font-mono text-[11px] tracking-[0.25em] text-phosphor">
        <span className="h-px w-8 bg-phosphor/60" />
        $ git branch -a --verbose --no-abbrev
      </div>
      <h2 className="rv font-display text-4xl font-bold tracking-tight text-ink sm:text-5xl" style={{ ["--rv-delay" as string]: "60ms" }}>
        Three branches.
        <br />
        <span className="text-dim">One survives the port matrix.</span>
      </h2>
      <p className="rv mt-5 max-w-2xl text-[15px] leading-relaxed text-dim" style={{ ["--rv-delay" as string]: "120ms" }}>
        The audit below is real: every number pulled from the repository itself. master is a
        desktop-GL scaffold; ame-next is a disciplined C23 rewrite; the agent branch is where
        the engine became a game — and the one whose every subsystem has a native web twin.
      </p>

      <div className="relative mt-14">
        {/* git rail */}
        <div className="absolute bottom-8 left-[7px] top-2 hidden w-px bg-gradient-to-b from-dim/40 via-ice/40 to-phosphor/70 md:block" />

        <div className="space-y-8">
          {BRANCHES.map((b, bi) => (
            <article
              key={b.name}
              className="rv relative md:pl-12"
              style={{ ["--rv-delay" as string]: `${bi * 90}ms` }}
            >
              {/* commit node */}
              <div
                className="absolute left-0 top-8 hidden h-[15px] w-[15px] rounded-full border-2 md:block"
                style={{ borderColor: b.color, background: "#04070a", boxShadow: b.selected ? `0 0 16px ${b.color}` : "none" }}
              />
              <div
                className={`cabinet relative overflow-hidden rounded-xl transition-transform duration-500 hover:-translate-y-1 ${
                  b.selected ? "ring-1 ring-phosphor/40" : ""
                }`}
              >
                {b.selected && (
                  <div className="absolute right-0 top-0 z-10 flex items-center gap-1.5 rounded-bl-lg bg-phosphor px-3 py-1.5 font-mono text-[10px] font-bold tracking-[0.15em] text-void">
                    <Trophy className="h-3 w-3" />
                    SELECTED FOR WEB
                  </div>
                )}
                <div className="border-b border-hairline px-5 py-3.5 font-mono text-[11px] text-dim sm:px-6">
                  <span className="text-dim/60">commit </span>
                  <span style={{ color: b.color }}>{b.short}</span>
                  <span className="mx-2 text-dim/40">·</span>
                  <span className="break-all text-ink/90">{b.name}</span>
                </div>

                <div className="grid gap-6 p-5 sm:p-6 lg:grid-cols-[1.35fr_1fr]">
                  <div>
                    <p className="text-[13.5px] leading-relaxed text-dim">{b.desc}</p>
                    <div className="mt-4 font-mono text-[10.5px] tracking-wide text-dim/80">
                      <span className="text-dim/50">stack: </span>
                      {b.stack}
                    </div>
                    <ul className="mt-5 space-y-2">
                      {b.fit.map((f) => (
                        <li key={f.label} className="flex items-start gap-2.5 text-[12.5px]">
                          {f.ok ? (
                            <Check className="mt-0.5 h-3.5 w-3.5 shrink-0 text-phosphor" />
                          ) : (
                            <X className="mt-0.5 h-3.5 w-3.5 shrink-0 text-ember" />
                          )}
                          <span className={f.ok ? "text-ink/85" : "text-dim"}>{f.label}</span>
                        </li>
                      ))}
                    </ul>
                  </div>

                  <div className="flex flex-col justify-between gap-5">
                    <div className="grid grid-cols-2 gap-2.5">
                      {b.stats.map((s) => (
                        <div key={s.k} className="rounded-lg border border-hairline bg-void/50 px-3 py-2.5">
                          <div className="flex items-center gap-1.5 font-mono text-[9.5px] uppercase tracking-wider text-dim/70">
                            {s.icon}
                            {s.k}
                          </div>
                          <div className="mt-1 font-mono text-lg font-semibold" style={{ color: b.color }}>
                            {s.v}
                          </div>
                        </div>
                      ))}
                    </div>
                    <Meter score={b.score} color={b.color} />
                    <div className="rounded-md border border-hairline bg-void/60 px-3.5 py-2.5 font-mono text-[10.5px] leading-relaxed" style={{ color: b.color }}>
                      <span className="text-dim/60">verdict: </span>
                      {b.verdict}
                    </div>
                  </div>
                </div>
              </div>
            </article>
          ))}
        </div>
      </div>

      {/* port matrix */}
      <div className="rv cabinet mt-10 overflow-hidden rounded-xl">
        <div className="flex items-center gap-2.5 border-b border-hairline px-5 py-3.5 font-mono text-[11px] tracking-wide text-ink sm:px-6">
          <ShieldCheck className="h-4 w-4 text-phosphor" />
          PORT MATRIX — how each engine subsystem reached the browser UNCHANGED
        </div>
        <div className="grid divide-y divide-hairline sm:grid-cols-2 sm:divide-x lg:grid-cols-3 lg:divide-y-0">
          {MODULE_MAP.map((m) => (
            <div key={m.c} className="px-5 py-4">
              <div className="font-mono text-[10px] uppercase tracking-wider text-dim/60">engine (c23)</div>
              <div className="font-mono text-[12px] text-ice">{m.c}</div>
              <div className="mt-2.5 font-mono text-[10px] uppercase tracking-wider text-dim/60">web</div>
              <div className="font-mono text-[12px] text-phosphor">{m.web}</div>
            </div>
          ))}
        </div>
      </div>
    </section>
  );
}
