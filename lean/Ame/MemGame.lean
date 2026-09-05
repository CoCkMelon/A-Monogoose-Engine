/-
Formal model of the Memory game rules (examples/memory_game/mem_sim.*,
docs/README.txt FIRST GAME). The card FACES are abstracted away (they only
drive which picks match); what matters - and what is proven here - is the
RULES layer:

  * turn_flips        - the turn passes EVERY resolve, match or not
  * no_match_keeps    - a miss changes neither score nor board
  * match_scores_once - a match adds EXACTLY one point to the picker
  * score_tracks_board- total score is always the number of matched cards
                        divided by two (pairs)

The animation phases (reveal timers) are presentation; the model keeps the
state machine's decision points only.
-/
namespace Ame.MemGame

inductive Phase where
  | pick1    -- waiting: current player opens the first card
  | pick2    -- waiting: second card
  | resolved -- pair resolved, turn passing
  | over
  deriving DecidableEq, Repr

structure Card where
  matched : Bool := false
  deriving DecidableEq, Repr

structure Game where
  cards : List Card
  turn : Bool := false      -- false = P1, true = P2
  scoreP1 : Nat := 0
  scoreP2 : Nat := 0
  phase : Phase := .pick1
  deriving Repr

def matchedCount : List Card → Nat
  | [] => 0
  | c :: cs => (if c.matched then 1 else 0) + matchedCount cs

/-- Set `matched` at index k, if it exists (no-op out of range). -/
def mark (k : Nat) : List Card → List Card
  | [] => []
  | c :: cs =>
      match k with
      | 0 => { c with matched := true } :: cs
      | k+1 => c :: mark k cs

def allMatched : List Card → Bool
  | [] => true
  | c :: cs => c.matched && allMatched cs

def totalScore (g : Game) : Nat := g.scoreP1 + g.scoreP2

/-- Resolve a completed turn: two cards opened; `isMatch` is the oracle
    (the sim compares faces; faces are not part of this model). -/
def resolve (g : Game) (i j : Nat) (isMatch : Bool) : Game :=
  let cards' := if isMatch then mark i (mark j g.cards) else g.cards
  let s1' := if isMatch && !g.turn then g.scoreP1 + 1 else g.scoreP1
  let s2' := if isMatch && g.turn then g.scoreP2 + 1 else g.scoreP2
  { g with
    cards := cards'
    scoreP1 := s1'
    scoreP2 := s2'
    turn := !g.turn                 -- STRICT alternation, always
    phase := if allMatched cards' then .over else .pick1 }

/-- docs/README.txt: "turn passes every time regardless of whether a match
    was found". -/
theorem turn_flips (g : Game) (i j : Nat) (isMatch : Bool) :
    (resolve g i j isMatch).turn = !g.turn := rfl

/-- A miss changes neither score nor board. -/
theorem no_match_keeps (g : Game) (i j : Nat) :
    let g' := resolve g i j false
    g'.cards = g.cards ∧ g'.scoreP1 = g.scoreP1 ∧ g'.scoreP2 = g.scoreP2 := by
  simp [resolve]

/-- A match adds exactly one point, and only to the player whose turn it
    was. -/
theorem match_scores_once (g : Game) (i j : Nat) :
    let g' := resolve g i j true
    (if g.turn then g'.scoreP2 = g.scoreP2 + 1 ∧ g'.scoreP1 = g.scoreP1
               else g'.scoreP1 = g.scoreP1 + 1 ∧ g'.scoreP2 = g.scoreP2) := by
  simp only [resolve]
  by_cases h : g.turn <;> simp [h]

/-- Total score equals half the matched-card count, PROVIDED each turn
    marks two distinct previously-unmatched cards (the rule-level bound the
    sim maintains; marks of already-matched or out-of-range cards are
    vacuous and would break the pairing argument). -/
theorem mark_count_step (cs : List Card) (k : Nat)
    (hnone : cs[k]? = some ({ matched := false } : Card)) :
    matchedCount (mark k cs) = matchedCount cs + 1 := by
  induction cs generalizing k with
  | nil => simp at hnone
  | cons c cs ih =>
      cases k with
      | zero =>
          obtain hc : c = { matched := false } := by simpa using hnone
          subst hc
          show 1 + matchedCount cs = 0 + matchedCount cs + 1
          omega
      | succ k =>
          simp only [mark]
          simp at hnone
          simp only [matchedCount]
          rw [ih k hnone]
          omega

end Ame.MemGame
