/-
  Semantic handle, matching include/ame/handle.h.

  C packing: bits 0..31 index, bits 32..63 generation.
  Generation 0 is INVALID. ame_handle_make(i, 0) returns 0.
-/
namespace Ame

structure Handle where
  index : Nat
  generation : Nat
  deriving DecidableEq, Repr

def Handle.invalid : Handle := ⟨0, 0⟩

@[simp] def Handle.ok (h : Handle) : Prop := h.generation ≠ 0

/-- C `ame_handle_make`: generation 0 collapses to invalid. -/
def Handle.mk' (index generation : Nat) : Handle :=
  if generation = 0 then .invalid else ⟨index, generation⟩

theorem mk'_zero (i : Nat) : Handle.mk' i 0 = Handle.invalid := rfl

theorem invalid_not_ok : ¬ Handle.invalid.ok := by
  simp [Handle.invalid]

theorem mk'_ok {i g : Nat} (h : g ≠ 0) : (Handle.mk' i g).ok := by
  simp [Handle.mk', h]

/-- Packed form used on the C side. Round-trip is shown on small values. -/
def Handle.pack (h : Handle) : UInt64 :=
  if h.generation = 0 then 0
  else (UInt64.ofNat h.generation <<< 32) ||| UInt64.ofNat h.index

def Handle.unpack (u : UInt64) : Handle :=
  let idx := (u &&& 0xffffffff).toNat
  let gen := (u >>> 32).toNat
  Handle.mk' idx gen

example : Handle.unpack (Handle.pack ⟨3, 7⟩) = ⟨3, 7⟩ := by native_decide
example : Handle.pack ⟨9, 0⟩ = 0 := rfl

/-- Cross-pool reference stored in events. poolId 0 = none/world. -/
structure Ref where
  poolId : Nat
  index : Nat
  generation : Nat
  deriving DecidableEq, Repr

def Ref.none : Ref := ⟨0, 0, 0⟩

def Ref.ofHandle (poolId : Nat) (h : Handle) : Ref :=
  ⟨poolId, h.index, h.generation⟩

def Ref.ok (r : Ref) : Prop := r.generation ≠ 0

theorem none_not_ok : ¬ Ref.none.ok := by simp [Ref.none, Ref.ok]

end Ame
