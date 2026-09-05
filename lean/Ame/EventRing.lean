/-
Formal model of the ame-next event queue (include/ame/events.h,
docs/events.txt): a bounded FIFO with DROP-OLDEST overflow.

Proves the two properties gameplay relies on:
  * push_bounded    - the queue never exceeds its compile-time capacity
  * push_no_drop    - under capacity, order is exactly FIFO (append)
-/
namespace Ame.EventRing

/-- One discrete gameplay event record (kind + payload elided). -/
structure Event where
  kind : Nat
  refA : Nat        -- cross-pool ref, modeled as a plain id
  refB : Nat        -- cross-pool ref, modeled as a plain id
  deriving DecidableEq, Repr

/-- The ring, modeled as its delivered order plus a capacity. -/
structure Ring where
  cap : Nat
  q : List Event
  overflow : Nat
  deriving Repr

/-- events_push: append, then drop from the FRONT while over capacity
    (drop-oldest; deterministic, bounded). -/
def push (r : Ring) (e : Event) : Ring :=
  let q' := r.q ++ [e]
  let drop := if q'.length ≤ r.cap then 0 else q'.length - r.cap
  { r with
    q := q'.drop drop
    overflow := r.overflow + drop }

/-- events_drain returns the queue in push order (and empties the ring). -/
def drain (r : Ring) : List Event × Ring :=
  (r.q, { r with q := [] })

theorem push_bounded (r : Ring) (e : Event) :
    (push r e).q.length ≤ r.cap := by
  simp only [push]
  by_cases h : (r.q ++ [e]).length ≤ r.cap
  · rw [if_pos h, List.drop_zero]
    exact h
  · rw [if_neg h, List.length_drop]
    have hlen : (r.q ++ [e]).length = r.q.length + 1 := by
      simp [List.length_append]
    rw [hlen] at h ⊢
    omega

theorem push_no_drop (r : Ring) (e : Event) (h : r.q.length < r.cap) :
    (push r e).q = r.q ++ [e] := by
  simp only [push]
  have hlen : (r.q ++ [e]).length = r.q.length + 1 := by
    simp [List.length_append]
  rw [hlen]
  have hc : r.q.length + 1 ≤ r.cap := by omega
  rw [if_pos hc, List.drop_zero]

/-- Under capacity nothing is dropped and the overflow counter is
    untouched. -/
theorem push_overflow_untouched (r : Ring) (e : Event) (h : r.q.length < r.cap) :
    (push r e).overflow = r.overflow := by
  simp only [push]
  have hlen : (r.q ++ [e]).length = r.q.length + 1 := by
    simp [List.length_append]
  rw [hlen]
  have hc : r.q.length + 1 ≤ r.cap := by omega
  rw [if_pos hc]
  omega

/-- At or over capacity at least one event is dropped and COUNTED
    (drop-oldest is never silent). -/
theorem push_overflow_counts_drop (r : Ring) (e : Event)
    (h : r.cap ≤ r.q.length) :
    (push r e).overflow ≥ r.overflow + 1 := by
  simp only [push]
  have hlen : (r.q ++ [e]).length = r.q.length + 1 := by
    simp [List.length_append]
  rw [hlen]
  by_cases hc : r.q.length + 1 ≤ r.cap
  · omega
  · rw [if_neg hc]
    omega

end Ame.EventRing
