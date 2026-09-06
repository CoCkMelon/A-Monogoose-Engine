import { useEffect, useMemo, useState } from "react";

const LINES = [
  "$ git clone --recurse-submodules CoCkMelon/A-Monogoose-Engine",
  "reviewing 3 branches ... master · ame-next-f2c36ea0 · agent/ame-next-4ad0fd06",
  "checkout agent/ame-next-20260905-4ad0fd06  [selected]",
  "emcc -std=gnu23 src/*.c examples/*.c  [C23 -> wasm32]",
  "--use-port=sdl3 ....................... windowing    [WebGL2 ctx]",
  "-sFULL_ES3=1 ......................... injected GL loader  [300 es]",
  "web shim ............ app_em.c · 1000 Hz inline grid  [pass]",
  "audio ............... pull-mode mix -> WebAudio  [4 voices]",
  "LINK  memory_game.wasm · raymarch.wasm · line_draw.wasm",
  "exec  wasm-runtime — exec",
];

export default function Boot({ onDone }: { onDone: () => void }) {
  const [shown, setShown] = useState(0);
  const [leaving, setLeaving] = useState(false);
  const total = LINES.length;

  const finish = useMemo(
    () => () => {
      setLeaving(true);
      setTimeout(onDone, 420);
    },
    [onDone]
  );

  useEffect(() => {
    const id = setInterval(() => {
      setShown((s) => {
        if (s + 1 >= total) {
          clearInterval(id);
          setTimeout(finish, 620);
        }
        return Math.min(s + 1, total);
      });
    }, 145);
    return () => clearInterval(id);
  }, [finish, total]);

  useEffect(() => {
    const skip = () => finish();
    window.addEventListener("pointerdown", skip);
    window.addEventListener("keydown", skip);
    return () => {
      window.removeEventListener("pointerdown", skip);
      window.removeEventListener("keydown", skip);
    };
  }, [finish]);

  return (
    <div
      className="fixed inset-0 z-[100] flex items-center justify-center bg-void transition-opacity duration-500"
      style={{ opacity: leaving ? 0 : 1, pointerEvents: leaving ? "none" : "auto" }}
      aria-hidden={leaving}
    >
      <div className="w-[min(680px,90vw)] font-mono text-[12px] leading-[1.9] sm:text-[13px]">
        <div className="mb-5 flex items-center gap-3 text-dim">
          <span className="inline-block h-2 w-2 rounded-full bg-phosphor pulse-dot" />
          ame-web · cross toolchain v2.6.0 · target: browser/wasm-js
        </div>
        {LINES.slice(0, shown).map((l, i) => {
          const bracketIdx = l.lastIndexOf("[");
          const head = bracketIdx > 0 ? l.slice(0, bracketIdx) : l;
          const tail = bracketIdx > 0 ? l.slice(bracketIdx) : "";
          return (
            <div key={i} className="boot-line whitespace-pre-wrap">
              <span className="text-phosphor/60">{String(i).padStart(2, "0")} </span>
              <span className={tail ? "text-ink" : "text-dim"}>{head}</span>
              {tail && <span className="text-phosphor">{tail}</span>}
            </div>
          );
        })}
        <span className="caret text-phosphor">▌</span>
      </div>
    </div>
  );
}
