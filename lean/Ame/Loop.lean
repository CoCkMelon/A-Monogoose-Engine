/-
  This game's loop, matching the locked v0 contract:

  * asyncinput callback owns hover/open (modelled as `click`)
  * main thread: tweens (omitted) then `resolve` after the hold,
    then a read-only snapshot, then render
  * no 1000 Hz logic thread
  * SETUP objects chain; HOT state is pools + this State
-/
import Ame.Memory
import Ame.Event
import Ame.Pool

namespace Ame.Loop

inductive Layer where
  | setup
  | hot
  deriving DecidableEq, Repr

inductive Owner where
  | callback
  | main
  deriving DecidableEq, Repr

/-- Input callback. -/
def onClick := Memory.click

/-- Main thread after HOLD_T. Tweens are not in the model. -/
def tick := Memory.resolve

/-- Render reads a copy; it never writes sim state. -/
def snapshot (s : Memory.State) : Memory.State := s

theorem snapshot_readonly (s : Memory.State) : snapshot s = s := rfl

theorem setFace_keeps_pair
    (s : Memory.State) (i j : Memory.CardIx) (f : Memory.Face) :
    ((Memory.setFace s i f).cards j).pair = (s.cards j).pair :=
  Memory.setFace_pair s i f j

theorem setFace2_keeps_pair
    (s : Memory.State) (a b j : Memory.CardIx) (f : Memory.Face) :
    ((Memory.setFace2 s a b f).cards j).pair = (s.cards j).pair :=
  Memory.setFace2_pair s a b f j

/-- One scripted turn: open a matching pair, resolve, snapshot. -/
theorem scripted_pair0 :
    let s := snapshot (tick (onClick (onClick Memory.start 0) 1))
    s.score0 = 1 ∧ s.turn = 1 ∧ s.nMatched = 1 :=
  ⟨Memory.pair0_score_p1, Memory.pair0_turn_passed, Memory.pair0_nMatched⟩

/-- Event kinds the C game pushes, in the order of a matching turn. -/
def matchTurnEvents : List Event.Kind :=
  [.open, .open, .match, .turn]

theorem match_turn_has_four : matchTurnEvents.length = 4 := rfl

end Ame.Loop
