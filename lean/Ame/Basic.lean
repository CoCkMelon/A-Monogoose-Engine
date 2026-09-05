/-
Copyright (c) 2025 ame-next contributors.
Formal model of the ame-next engine core: pure Lean 4 (no mathlib).
-/

/- The fixed-step determinism contract (docs/loop.txt, principles.txt):
    the simulation is a pure state machine; one `step` per fixed tick
    (dt is constant at 1000 Hz), fed a list of inputs. The same start
    state and the same inputs reproduce the same trajectory - which is
    what the golden replay tests rely on (per-binary determinism). -/

namespace Ame

section Determinism

variable {σ α : Type} (step : σ → α → σ)

/-- Run a state machine over an input list from a start state. -/
def run (s : σ) : List α → σ :=
  fun l => l.foldl (fun st a => step st a) s

/-- Determinism, usefully: two step functions that agree on every
    state and input produce identical runs, so replaying recorded
    inputs through an identical machine reproduces the trajectory. -/
theorem run_deterministic {step step' : σ → α → σ}
    (h : ∀ s a, step s a = step' s a) (s : σ) (l : List α) :
    run step s l = run step' s l := by
  induction l generalizing s with
  | nil => rfl
  | cons a l ih =>
      simp only [run, List.foldl_cons] at ih ⊢
      rw [h s a, ih (step' s a)]

/-- Replay composition: running l₁ then l₂ equals running the
    concatenation (checkpoints, replays, server authority). -/
theorem run_append (s : σ) (l₁ l₂ : List α) :
    run step (run step s l₁) l₂ = run step s (l₁ ++ l₂) := by
  simp only [run, List.foldl_append]

end Determinism

end Ame
