import { useCallback, useEffect, useRef, useState } from "react";
import { Loader2, RefreshCw, TriangleAlert } from "lucide-react";

/* Hosts a real Emscripten build of the C23 engine. The glue+wasm are baked
 * into THIS page (-sSINGLE_FILE=1, shipped as ?raw text, executed from a
 * blob: URL) so the site works even when the host serves only index.html —
 * there are no sibling file requests that could 404. */

declare global {
  interface Window {
    AmeMemory?: AmeFactory;
    AmeRm?: AmeFactory;
    AmeLine?: AmeFactory;
  }
}
type AmeFactory = (opts: Record<string, unknown>) => Promise<AmeModule>;
interface AmeModule {
  _ame_set_running?: (r: number) => void;
  _ame_audio_resume?: () => void;
}

interface Props {
  rawJs: string; // single-file emscripten glue (wasm embedded base64)
  factory: "AmeMemory" | "AmeRm" | "AmeLine";
  canvasId: string;
  active: boolean;
}

function probeWebGL2(): string | null {
  try {
    const c = document.createElement("canvas");
    const gl = c.getContext("webgl2");
    if (!gl) return "WebGL2 context could not be created (GPU disabled or blocked)";
    return null;
  } catch (e) {
    return `WebGL2 probe threw: ${String(e)}`;
  }
}

export default function WasmGame({ rawJs, factory, canvasId, active }: Props) {
  const [phase, setPhase] = useState<"loading" | "ready" | "error">("loading");
  const [reason, setReason] = useState("");
  const [attempt, setAttempt] = useState(0);
  const modRef = useRef<AmeModule | null>(null);
  const blobUrl = useRef<string | null>(null);

  const boot = useCallback(async () => {
    setPhase("loading");
    setReason("");
    const glProblem = probeWebGL2();
    if (glProblem) {
      setPhase("error");
      setReason(glProblem);
      return;
    }
    if (!rawJs) {
      setPhase("error");
      setReason("wasm bundle missing from this build");
      return;
    }
    try {
      /* fresh module scope per boot attempt + per game instance */
      if (!blobUrl.current) {
        blobUrl.current = URL.createObjectURL(
          new Blob([rawJs], { type: "text/javascript" })
        );
      }
      await new Promise<void>((res, rej) => {
        const s = document.createElement("script");
        s.src = blobUrl.current!;
        s.onload = () => res();
        s.onerror = () => rej(new Error("glue script failed to execute"));
        document.head.appendChild(s);
      });
      const f = window[factory];
      if (!f) throw new Error(`module factory ${factory} not found after glue eval`);
      const m = await f({
        noExitRuntime: true,
        onAbort: (what: unknown) => {
          setPhase("error");
          setReason(`runtime aborted: ${String(what)}`);
        },
      });
      modRef.current = m;
      setPhase("ready");
    } catch (e) {
      console.error("[ame-web] wasm boot failed:", e);
      setPhase("error");
      setReason(String((e as Error)?.message ?? e));
    }
  }, [rawJs, factory]);

  useEffect(() => {
    let dead = false;
    void (async () => {
      if (dead) return;
      await boot();
    })();
    return () => {
      dead = true;
      modRef.current = null; /* SDL owns its loop; park via set_running(0) */
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [boot, attempt]);

  /* pause/resume the sim with cabinet tab focus */
  useEffect(() => {
    if (phase === "ready") modRef.current?._ame_set_running?.(active ? 1 : 0);
  }, [active, phase]);

  return (
    <div
      className="relative h-full w-full overflow-hidden bg-[#0b0e12]"
      onPointerDown={() => modRef.current?._ame_audio_resume?.()}
    >
      <canvas id={canvasId} className="h-full w-full object-contain" />
      {phase === "loading" && (
        <div className="absolute inset-0 z-10 flex items-center justify-center gap-2.5 bg-void/80 font-mono text-[11px] text-dim">
          <Loader2 className="h-4 w-4 animate-spin text-phosphor" />
          instantiating wasm…
        </div>
      )}
      {phase === "error" && (
        <div className="absolute inset-0 z-10 flex flex-col items-center justify-center gap-3 bg-void/90 px-6 text-center font-mono text-[11px] text-ember">
          <TriangleAlert className="h-5 w-5" />
          <div>wasm module failed to start</div>
          <div className="max-w-md break-words text-[10px] leading-relaxed text-dim">
            {reason}
          </div>
          <button
            onClick={() => setAttempt((a) => a + 1)}
            className="mt-1 flex items-center gap-2 rounded-md border border-hairline bg-panel/70 px-4 py-2 text-[10px] tracking-wider text-ink transition-colors hover:border-phosphor/50 hover:text-phosphor"
          >
            <RefreshCw className="h-3.5 w-3.5" />
            RETRY
          </button>
        </div>
      )}
    </div>
  );
}
