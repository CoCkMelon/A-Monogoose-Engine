/-
Formal model of ame-next pools (include/ame/pool.h, docs/data.txt).

The C template gives every pool: generation handles, allocation from a
fixed free list, DEFERRED frees (safe during iteration/drain), and
stale-handle invalidation. This file mirrors that contract and proves:

  * alloc_none_when_full    - a full pool returns none (never a sentinel)
  * alloc_valid             - a fresh handle validates in the new state
  * valid_untouched_by_free - deferring a free changes nothing observable
  * applyOne_invalidates    - applying a free BUMPS the generation, so the
                              STALE handle can never validate again

State is modeled with plain functions (Nat → …) instead of arrays so the
proofs stay about the CONTRACT, not memory layout.
-/
namespace Ame.Pool

/-- Handle = (index, generation). Generation 0 is invalid by construction. -/
structure Handle where
  idx : Nat
  gen : Nat
  deriving DecidableEq, Repr

/-- Slot bookkeeping of one pool with capacity n (data.txt). -/
structure Pool (n : Nat) where
  gen : Nat → Nat          -- per-slot generation (0 = never used)
  alive : Nat → Bool
  free : List Nat          -- free-slot stack
  pending : List Handle    -- deferred free requests

/-- Handle validity, exactly as pool.h's slots_valid. -/
def valid {n : Nat} (p : Pool n) (h : Handle) : Bool :=
  h.gen != 0 && h.idx < n && p.alive h.idx && p.gen h.idx == h.gen

/-- Function update used for slot writes. -/
def upd (f : Nat → α) (k : Nat) (v : α) : Nat → α :=
  fun i => if i = k then v else f i

@[simp] theorem upd_self (f : Nat → α) (k : Nat) (v : α) :
    upd f k v k = v := by
  simp [upd]

@[simp] theorem upd_other (f : Nat → α) (k i : Nat) (v : α) (h : i ≠ k) :
    upd f k v i = f i := by
  simp [upd, h]

/-- Fresh pool: nothing alive, all slots free, no pending requests. -/
def init (n : Nat) : Pool n where
  gen := fun _ => 0
  alive := fun _ => false
  free := (List.range n).reverse
  pending := []

/-- Well-formedness used by allocation: the free list only holds in-range
    indices (the C template builds it from 0..<cap). -/
def InRange {n : Nat} (p : Pool n) : Prop := ∀ i ∈ p.free, i < n

section Alloc

/-- The cons-branch of alloc, as its own definition so lemmas compose. -/
def allocFrom {n : Nat} (p : Pool n) (i : Nat) (rest : List Nat) :
    Handle × Pool n :=
  -- generations start at 1 (0 = invalid); on reuse the slot keeps its gen
  let g := max 1 (p.gen i)
  (⟨i, g⟩,
   { p with
     gen := upd p.gen i g
     alive := upd p.alive i true
     free := rest })

/-- pool_spawn: take the top free slot (or none when full - never -1). -/
def alloc {n : Nat} (p : Pool n) : Option (Handle × Pool n) :=
  match p.free with
  | [] => none
  | i :: rest => some (allocFrom p i rest)

@[simp] theorem alloc_none_when_full {n : Nat} (p : Pool n) (h : p.free = []) :
    alloc p = none := by
  rw [alloc, h]

theorem allocFrom_valid {n : Nat} (p : Pool n) (i : Nat) (rest : List Nat)
    (hin : i < n) : valid (allocFrom p i rest).2 (allocFrom p i rest).1 = true := by
  simp [allocFrom, valid, upd_self, hin]

theorem alloc_valid {n : Nat} (p : Pool n) (hp : InRange p) (h : Handle)
    (p' : Pool n) (ha : alloc p = some (h, p')) : valid p' h = true := by
  cases hf : p.free with
  | nil => rw [alloc_none_when_full p hf] at ha; exact absurd ha (by simp)
  | cons i rest =>
      have hin : i < n := hp i (by rw [hf]; exact List.Mem.head _)
      rw [alloc, hf] at ha
      simp only at ha
      obtain ⟨hE, pE⟩ := Prod.mk.inj (Option.some.inj ha)
      subst hE
      subst pE
      exact allocFrom_valid p i rest hin

end Alloc

section DeferredFrees

/-- pool_despawn: REQUEST a despawn - deferred, nothing observable changes. -/
def requestFree {n : Nat} (p : Pool n) (h : Handle) : Pool n :=
  { p with pending := p.pending ++ [h] }

/-- A deferred request never changes validity of ANY handle (the slot stays
    alive until applyFrees). -/
theorem valid_untouched_by_free {n : Nat} (p : Pool n) (h h' : Handle) :
    valid (requestFree p h) h' = valid p h' :=
  rfl

/-- Apply one free: mark dead and BUMP the generation. -/
def applyOne {n : Nat} (p : Pool n) (h : Handle) : Pool n :=
  { p with
    alive := upd p.alive h.idx false
    gen := upd p.gen h.idx (p.gen h.idx + 1) }

/-- The key safety property (data.txt rule 3): after the generation bump,
    the OLD handle is dead forever - a stale reference fails loudly. -/
theorem applyOne_invalidates {n : Nat} (p : Pool n) (h : Handle) :
    valid (applyOne p h) h = false := by
  simp only [valid, applyOne, upd_self]
  simp

/-- The bump is visible exactly at the freed slot; every other slot keeps
    its generation (so OTHER handles stay valid). -/
theorem applyOne_gen {n : Nat} (p : Pool n) (h : Handle) (k : Nat)
    (hnek : k ≠ h.idx) : (applyOne p h).gen k = p.gen k := by
  simp only [applyOne, upd_other p.gen h.idx k _ hnek]

end DeferredFrees

end Ame.Pool
