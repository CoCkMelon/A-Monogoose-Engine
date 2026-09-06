/* sfx.ts — WebAudio synth mirroring the engine's ame_synth_cfg voices.
 * Config values are lifted verbatim from examples/memory_game/mem_app.c
 * (flip 660 Hz triangle, match 880 Hz sine, miss 180 Hz saw, win 440 Hz sq). */

type Wave = "sine" | "triangle" | "sawtooth" | "square";

interface SynthCfg {
  wave: Wave;
  freq: number;
  gain: number;
  attack: number;
  hold: number;
  release: number;
  slide?: number; // semitone offset over the voice (web extension)
}

const V = {
  /* exact engine voices */
  flip: { wave: "triangle", freq: 660, gain: 0.25, attack: 0.002, hold: 0.03, release: 0.05 },
  match: { wave: "sine", freq: 880, gain: 0.3, attack: 0.005, hold: 0.08, release: 0.12 },
  miss: { wave: "sawtooth", freq: 180, gain: 0.18, attack: 0.004, hold: 0.05, release: 0.1 },
  win: { wave: "square", freq: 440, gain: 0.2, attack: 0.01, hold: 0.2, release: 0.3 },
  /* arcade voices in the same vocabulary */
  hit: { wave: "sawtooth", freq: 120, gain: 0.3, attack: 0.002, hold: 0.06, release: 0.14 },
  spawn: { wave: "sine", freq: 1320, gain: 0.05, attack: 0.001, hold: 0.015, release: 0.03 },
  ui: { wave: "triangle", freq: 520, gain: 0.12, attack: 0.002, hold: 0.02, release: 0.05 },
  turn: { wave: "square", freq: 330, gain: 0.08, attack: 0.002, hold: 0.02, release: 0.05 },
  lose: { wave: "sawtooth", freq: 220, gain: 0.22, attack: 0.005, hold: 0.25, release: 0.5, slide: -12 },
} satisfies Record<string, SynthCfg>;

class Sfx {
  private ctx: AudioContext | null = null;
  private master: GainNode | null = null;
  muted = false;

  private ensure(): AudioContext | null {
    if (typeof window === "undefined") return null;
    if (!this.ctx) {
      const AC = window.AudioContext || (window as any).webkitAudioContext;
      if (!AC) return null;
      this.ctx = new AC();
      this.master = this.ctx.createGain();
      this.master.gain.value = 0.9;
      this.master.connect(this.ctx.destination);
    }
    if (this.ctx.state === "suspended") void this.ctx.resume();
    return this.ctx;
  }

  /** call from any user gesture to unlock audio */
  unlock() {
    this.ensure();
  }

  private voice(cfg: SynthCfg, when = 0, freqMul = 1) {
    if (this.muted) return;
    const ctx = this.ensure();
    if (!ctx || !this.master) return;
    const t0 = ctx.currentTime + when;
    const osc = ctx.createOscillator();
    const g = ctx.createGain();
    osc.type = cfg.wave;
    osc.frequency.setValueAtTime(cfg.freq * freqMul, t0);
    if (cfg.slide) {
      osc.frequency.exponentialRampToValueAtTime(
        Math.max(24, cfg.freq * freqMul * Math.pow(2, cfg.slide / 12)),
        t0 + cfg.attack + cfg.hold + cfg.release
      );
    }
    g.gain.setValueAtTime(0, t0);
    g.gain.linearRampToValueAtTime(cfg.gain, t0 + cfg.attack);
    g.gain.setValueAtTime(cfg.gain, t0 + cfg.attack + cfg.hold);
    g.gain.exponentialRampToValueAtTime(0.0004, t0 + cfg.attack + cfg.hold + cfg.release);
    osc.connect(g).connect(this.master);
    osc.start(t0);
    osc.stop(t0 + cfg.attack + cfg.hold + cfg.release + 0.05);
  }

  flip() { this.voice(V.flip); }
  match() { this.voice(V.match); this.voice(V.match, 0.085, 1.335); } // major third up
  miss() { this.voice(V.miss); }
  win() {
    [0, 4, 7, 12].forEach((st, i) => this.voice(V.win, i * 0.11, Math.pow(2, st / 12)));
  }
  loseTheme() { this.voice(V.lose); this.voice(V.lose, 0.22, 0.5); }
  hit() { this.voice(V.hit); }
  spawn() { this.voice(V.spawn); }
  ui() { this.voice(V.ui); }
  turn() { this.voice(V.turn); }
}

export const sfx = new Sfx();
