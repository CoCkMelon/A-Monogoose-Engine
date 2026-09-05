/-
  Fixed-capacity pool. Matches src/pool.c:

  * spawn first free slot, bump generation (skip 0)
  * despawn queues; applyDespawns runs at step end
  * stale handle fails after reuse
-/
import Ame.Handle

namespace Ame.Pool

structure Pool (n : Nat) where
  gen : Fin n → Nat
  alive : Fin n → Bool
  pending : List (Fin n)

def empty (n : Nat) : Pool n :=
  { gen := fun _ => 0, alive := fun _ => false, pending := [] }

def live {n : Nat} (p : Pool n) : Nat :=
  (List.finRange n).foldl (fun acc i => if p.alive i then acc + 1 else acc) 0

def valid {n : Nat} (p : Pool n) (h : Handle) : Prop :=
  h.generation ≠ 0 ∧
    ∃ i : Fin n, i.val = h.index ∧ p.alive i = true ∧ p.gen i = h.generation

/-- Spawn into a known free (or any) slot, like finding `!alive[i]`. -/
def spawnAt {n : Nat} (p : Pool n) (i : Fin n) : Pool n × Handle :=
  let g := p.gen i + 1
  ({ gen := fun j => if j = i then g else p.gen j
     alive := fun j => if j = i then true else p.alive j
     pending := p.pending },
   ⟨i.val, g⟩)

def despawnIdx {n : Nat} (p : Pool n) (i : Fin n) : Pool n :=
  if p.alive i then { p with pending := p.pending ++ [i] } else p

def applyDespawns {n : Nat} (p : Pool n) : Pool n :=
  { gen := p.gen
    alive := fun j => if j ∈ p.pending then false else p.alive j
    pending := [] }

theorem spawnAt_ok {n : Nat} (p : Pool n) (i : Fin n) :
    valid (spawnAt p i).1 (spawnAt p i).2 := by
  refine ⟨Nat.succ_ne_zero _, ⟨i, rfl, ?_, ?_⟩⟩
  · simp [spawnAt]
  · simp [spawnAt]

theorem spawnAt_index {n : Nat} (p : Pool n) (i : Fin n) :
    (spawnAt p i).2.index = i.val := rfl

theorem spawnAt_gen_succ {n : Nat} (p : Pool n) (i : Fin n) :
    (spawnAt p i).2.generation = p.gen i + 1 := rfl

/-- Despawn does not free the slot until apply. -/
theorem despawn_still_valid {n : Nat} (p : Pool n) (i : Fin n)
    (ha : p.alive i = true) (hg : p.gen i ≠ 0) :
    valid (despawnIdx p i) ⟨i.val, p.gen i⟩ := by
  refine ⟨hg, ⟨i, rfl, ?_, ?_⟩⟩
  · simp [despawnIdx, ha]
  · simp [despawnIdx, ha]

theorem apply_kills {n : Nat} (p : Pool n) (i : Fin n)
    (hp : i ∈ p.pending) (hg : p.gen i ≠ 0) :
    ¬ valid (applyDespawns p) ⟨i.val, p.gen i⟩ := by
  intro h
  rcases h with ⟨_, j, hj, halive, _⟩
  have hj' : j = i := Fin.eq_of_val_eq hj
  subst hj'
  simp [applyDespawns, hp] at halive

/-- Reuse of a slot bumps generation, so the old handle is stale. -/
theorem reuse_stale {n : Nat} (p : Pool n) (i : Fin n)
    (hpend : p.pending = []) :
    (spawnAt p i).2.generation <
        (spawnAt (applyDespawns (despawnIdx (spawnAt p i).1 i)) i).2.generation ∧
      ¬ valid
          (spawnAt (applyDespawns (despawnIdx (spawnAt p i).1 i)) i).1
          (spawnAt p i).2 := by
  constructor
  · simp [spawnAt, applyDespawns, despawnIdx, hpend]
  · intro hv
    rcases hv with ⟨_, j, hj, _, hgen⟩
    have hj' : j = i := Fin.eq_of_val_eq hj
    subst hj'
    simp [spawnAt, applyDespawns, despawnIdx, hpend] at hgen
    try omega

end Ame.Pool
