import { useEffect, useState } from "react";
import { GitBranch, Volume2, VolumeX } from "lucide-react";
import { sfx } from "../lib/sfx";

function GithubMark({ className }: { className?: string }) {
  return (
    <svg viewBox="0 0 16 16" fill="currentColor" className={className} aria-hidden>
      <path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27s1.36.09 2 .27c1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.01 8.01 0 0 0 16 8c0-4.42-3.58-8-8-8Z" />
    </svg>
  );
}

const LINKS = [
  { href: "#branches", label: "BRANCHES" },
  { href: "#arcade", label: "ARCADE" },
  { href: "#architecture", label: "ARCHITECTURE" },
];

export function Nav() {
  const [scrolled, setScrolled] = useState(false);
  const [muted, setMuted] = useState(false);
  useEffect(() => {
    const on = () => setScrolled(window.scrollY > 40);
    on();
    window.addEventListener("scroll", on, { passive: true });
    return () => window.removeEventListener("scroll", on);
  }, []);
  return (
    <nav
      className={`fixed inset-x-0 top-0 z-[80] transition-all duration-500 ${
        scrolled ? "border-b border-hairline bg-void/80 backdrop-blur-md" : "bg-transparent"
      }`}
    >
      <div className="mx-auto flex max-w-6xl items-center justify-between px-5 py-3.5 sm:px-8">
        <a href="#top" className="group flex items-center gap-2.5">
          <svg width="26" height="26" viewBox="0 0 32 32">
            <circle cx="16" cy="16" r="10" fill="none" stroke="#33ff80" strokeWidth="2.4" />
            <circle cx="16" cy="16" r="3.2" fill="#33ff80" />
          </svg>
          <span className="font-mono text-sm font-bold tracking-wide text-ink">
            AME<span className="text-phosphor">//</span>WEB
          </span>
        </a>
        <div className="hidden items-center gap-6 md:flex">
          {LINKS.map((l) => (
            <a
              key={l.href}
              href={l.href}
              className="font-mono text-[11px] tracking-[0.18em] text-dim transition-colors hover:text-phosphor"
            >
              {l.label}
            </a>
          ))}
        </div>
        <div className="flex items-center gap-3">
          <button
            onClick={() => {
              sfx.muted = !sfx.muted;
              setMuted(sfx.muted);
              if (!sfx.muted) {
                sfx.unlock();
                sfx.ui();
              }
            }}
            className="flex h-8 w-8 items-center justify-center rounded-md border border-hairline text-dim transition-colors hover:border-phosphor/50 hover:text-phosphor"
            aria-label="toggle sound"
          >
            {muted ? <VolumeX className="h-3.5 w-3.5" /> : <Volume2 className="h-3.5 w-3.5" />}
          </button>
          <a
            href="https://github.com/CoCkMelon/A-Monogoose-Engine"
            target="_blank"
            rel="noreferrer"
            className="flex items-center gap-2 rounded-md border border-hairline px-3 py-1.5 font-mono text-[10px] tracking-wide text-dim transition-colors hover:border-phosphor/50 hover:text-phosphor"
          >
          <GithubMark className="h-3.5 w-3.5" />
          <span className="hidden sm:inline">SOURCE</span>
          </a>
        </div>
      </div>
    </nav>
  );
}

const MARQUEE_ITEMS = [
  "xorshift32 rng", "seqlock snapshots", "pool + generation handles", "one-batch renderer",
  "fixed-step 1000 Hz", "uniform-grid broadphase", "opus decode", "lean 4 proofs",
  "shadow maps", "server-authoritative net", "gl_line_strip", "deterministic replay",
  "synth voice table", "sdf raymarching",
];

export function Marquee() {
  const row = (
    <div className="flex shrink-0 items-center">
      {MARQUEE_ITEMS.map((m) => (
        <span key={m} className="flex items-center font-mono text-[11px] uppercase tracking-[0.22em] text-dim">
          <span className="px-6">{m}</span>
          <GitBranch className="h-3 w-3 text-phosphor/50" />
        </span>
      ))}
    </div>
  );
  return (
    <div className="relative border-y border-hairline bg-carbon/60 py-3.5">
      <div className="marquee-track">
        {row}
        {row}
      </div>
      <div className="pointer-events-none absolute inset-y-0 left-0 w-24 bg-gradient-to-r from-void to-transparent" />
      <div className="pointer-events-none absolute inset-y-0 right-0 w-24 bg-gradient-to-l from-void to-transparent" />
    </div>
  );
}

export function Footer() {
  return (
    <footer className="relative border-t border-hairline bg-carbon/40">
      <div className="mx-auto max-w-6xl px-5 py-14 sm:px-8">
        <div className="grid gap-10 md:grid-cols-[1.3fr_1fr_1fr]">
          <div>
            <div className="flex items-center gap-2.5">
              <svg width="30" height="30" viewBox="0 0 32 32">
                <circle cx="16" cy="16" r="10" fill="none" stroke="#33ff80" strokeWidth="2.4" />
                <circle cx="16" cy="16" r="3.2" fill="#33ff80" />
              </svg>
              <span className="font-display text-xl font-bold text-ink">AME//WEB</span>
            </div>
            <p className="mt-4 max-w-sm text-[13px] leading-relaxed text-dim">
              A branch audit and web port of{" "}
              <a
                href="https://github.com/CoCkMelon/A-Monogoose-Engine"
                target="_blank"
                rel="noreferrer"
                className="text-phosphor underline decoration-phosphor/40 underline-offset-2 hover:decoration-phosphor"
              >
                CoCkMelon/A-Monogoose-Engine
              </a>
              . Every gameplay constant, synth voice, and rule is transcribed from the
              original C sources — the bugs are ours, the mongoose is theirs.
            </p>
          </div>
          <div>
            <div className="font-mono text-[10px] tracking-[0.25em] text-dim/60">BRANCHES AUDITED</div>
            <ul className="mt-3 space-y-2 font-mono text-[11px] text-dim">
              <li>master <span className="text-dim/50">· 97 commits</span></li>
              <li>ame-next-f2c36ea03349a2ca <span className="text-dim/50">· +3</span></li>
              <li className="text-phosphor">agent/ame-next-20260905-4ad0fd06 ✓</li>
            </ul>
          </div>
          <div>
            <div className="font-mono text-[10px] tracking-[0.25em] text-dim/60">TOOLCHAIN</div>
            <ul className="mt-3 space-y-2 font-mono text-[11px] text-dim">
              <li>React 19 + TypeScript strict</li>
              <li>WebGL2 · Canvas2D · WebAudio</li>
              <li>Tailwind 4 · single-file build</li>
            </ul>
          </div>
        </div>
        <div className="mt-12 flex flex-wrap items-center justify-between gap-4 border-t border-hairline pt-6 font-mono text-[10px] tracking-wider text-dim/50">
          <span>exit code 0 — session complete</span>
          <span>MIT · engine sources © CoCkMelon · web port is an unofficial tribute</span>
        </div>
      </div>
    </footer>
  );
}
