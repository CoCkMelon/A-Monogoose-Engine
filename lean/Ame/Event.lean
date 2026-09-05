/-
  Bounded discrete event queue. Matches src/events.c:

  * FIFO ring of cap 256
  * overflow drops the oldest record
  * drain returns push order and empties
-/
namespace Ame.Event

inductive Kind where
  | overlapEnter
  | overlapExit
  | hazard
  | impact
  | open
  | match
  | mismatch
  | turn
  | win
  deriving DecidableEq, Repr

structure Ev where
  kind : Kind
  deriving DecidableEq, Repr

def cap : Nat := 256

structure Queue where
  items : List Ev
  overflows : Nat
  bounded : items.length ≤ cap

def empty : Queue :=
  ⟨[], 0, Nat.zero_le _⟩

def push (q : Queue) (e : Ev) : Queue :=
  if h : q.items.length = cap then
    have hlen : (q.items.drop 1 ++ [e]).length ≤ cap := by
      have : q.items.length = 256 := h
      simp [List.length_append, List.length_drop, this, cap]
    ⟨q.items.drop 1 ++ [e], q.overflows + 1, hlen⟩
  else
    have hlen : (q.items ++ [e]).length ≤ cap := by
      have hb : q.items.length ≤ cap := q.bounded
      have hn : q.items.length ≠ cap := h
      simp [List.length_append]
      omega
    ⟨q.items ++ [e], q.overflows, hlen⟩

def drain (q : Queue) : List Ev × Queue :=
  (q.items, ⟨[], q.overflows, Nat.zero_le _⟩)

theorem drain_order (q : Queue) : (drain q).1 = q.items := rfl

theorem drain_empty (q : Queue) : (drain q).2.items = [] := rfl

theorem push_room (q : Queue) (e : Ev) (h : q.items.length < cap) :
    (push q e).items = q.items ++ [e] := by
  unfold push
  split
  · next heq => omega
  · rfl

theorem push_full (q : Queue) (e : Ev) (h : q.items.length = cap) :
    (push q e).items = q.items.drop 1 ++ [e] := by
  unfold push
  split
  · rfl
  · next hne => exact (hne h).elim

theorem push_full_overflow (q : Queue) (e : Ev) (h : q.items.length = cap) :
    (push q e).overflows = q.overflows + 1 := by
  unfold push
  split
  · rfl
  · next hne => exact (hne h).elim

theorem push_stays_bounded (q : Queue) (e : Ev) :
    (push q e).items.length ≤ cap :=
  (push q e).bounded

end Ame.Event
