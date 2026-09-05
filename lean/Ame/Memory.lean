/-
  Local hotseat Memory. Matches include/ame/memory.h + src/memory.c.

  * 4×4, 8 pairs
  * callback: click (open a down card)
  * main: resolve after the hold (modelled as an explicit step)
  * match scores, turn still passes; mismatch closes, turn passes
  * winner is most matches; 4–4 is a tie
-/
namespace Ame.Memory

abbrev CardIx := Fin 16
abbrev PairId := Fin 8
abbrev Seat := Fin 2

inductive Face where
  | down
  | up
  | matched
  deriving DecidableEq, Repr, Inhabited

structure Card where
  pair : PairId
  face : Face
  deriving DecidableEq, Repr

inductive Phase where
  | idle
  | oneOpen (i : CardIx)
  | resolving (a b : CardIx)
  deriving DecidableEq, Repr

inductive Outcome where
  | playing
  | winner (s : Seat)
  | tie
  deriving DecidableEq, Repr

structure State where
  cards : CardIx → Card
  turn : Seat
  score0 : Nat
  score1 : Nat
  phase : Phase
  outcome : Outcome
  nMatched : Nat

def setFace (s : State) (i : CardIx) (f : Face) : State :=
  { s with cards := fun j =>
      if j = i then { s.cards j with face := f } else s.cards j }

def setFace2 (s : State) (a b : CardIx) (f : Face) : State :=
  setFace (setFace s a f) b f

def other (t : Seat) : Seat := t + 1

def addScore (s : State) (t : Seat) : State :=
  if t = 0 then { s with score0 := s.score0 + 1 }
  else { s with score1 := s.score1 + 1 }

def finish (s : State) : Outcome :=
  if s.score0 > s.score1 then .winner 0
  else if s.score1 > s.score0 then .winner 1
  else .tie

theorem val_div2_lt_8 (i : CardIx) : i.val / 2 < 8 := by
  have := i.isLt
  omega

/-- Pair id i/2: cards 0,1 are pair 0, …, 14,15 are pair 7. -/
def canon (i : CardIx) : PairId := ⟨i.val / 2, val_div2_lt_8 i⟩

def start : State where
  cards := fun i => ⟨canon i, .down⟩
  turn := 0
  score0 := 0
  score1 := 0
  phase := .idle
  outcome := .playing
  nMatched := 0

def click (s : State) (i : CardIx) : State :=
  if s.outcome ≠ .playing then s
  else
    match s.phase with
    | .resolving _ _ => s
    | .idle =>
        if (s.cards i).face = .down then
          { setFace s i .up with phase := .oneOpen i }
        else s
    | .oneOpen a =>
        if i = a then s
        else if (s.cards i).face = .down then
          { setFace s i .up with phase := .resolving a i }
        else s

def resolve (s : State) : State :=
  match s.outcome, s.phase with
  | .playing, .resolving a b =>
      if (s.cards a).pair = (s.cards b).pair then
        let s1 := addScore (setFace2 s a b .matched) s.turn
        let s2 : State := { s1 with phase := .idle, nMatched := s.nMatched + 1 }
        if s2.nMatched = 8 then { s2 with outcome := finish s2 }
        else { s2 with turn := other s.turn }
      else
        { setFace2 s a b .down with
          phase := .idle
          turn := other s.turn }
  | _, _ => s

@[simp] theorem setFace_pair (s : State) (i : CardIx) (f : Face) (j : CardIx) :
    ((setFace s i f).cards j).pair = (s.cards j).pair := by
  simp [setFace]
  by_cases h : j = i
  · simp [h]
  · simp [h]

@[simp] theorem setFace2_pair (s : State) (a b : CardIx) (f : Face) (j : CardIx) :
    ((setFace2 s a b f).cards j).pair = (s.cards j).pair := by
  simp [setFace2]

@[simp] theorem addScore_cards (s : State) (t : Seat) (j : CardIx) :
    (addScore s t).cards j = s.cards j := by
  by_cases h : t = 0
  · simp [addScore, h]
  · simp [addScore, h]

theorem setFace_face_at (s : State) (i : CardIx) (f : Face) :
    ((setFace s i f).cards i).face = f := by
  simp [setFace]

theorem click_ignored_resolving
    (s : State) (a b i : CardIx)
    (h : s.phase = .resolving a b) :
    click s i = s := by
  simp [click, h]

theorem click_ignored_over
    (s : State) (i : CardIx)
    (h : s.outcome ≠ .playing) :
    click s i = s := by
  simp [click, h]

theorem click_opens_idle
    (s : State) (i : CardIx)
    (ho : s.outcome = .playing)
    (hp : s.phase = .idle)
    (hd : (s.cards i).face = .down) :
    (click s i).phase = .oneOpen i ∧
      ((click s i).cards i).face = .up := by
  unfold click
  simp [ho, hp, hd, setFace]

theorem resolve_noop_idle (s : State) (h : s.phase = .idle) :
    resolve s = s := by
  unfold resolve
  rw [h]
  cases s.outcome <;> rfl

theorem start_all_down (i : CardIx) : (start.cards i).face = .down := rfl

theorem start_idle : start.phase = .idle := rfl

theorem start_playing : start.outcome = .playing := rfl

theorem start_zero_score : start.score0 = 0 ∧ start.score1 = 0 := ⟨rfl, rfl⟩

theorem canon_pair0 : canon (0 : CardIx) = canon (1 : CardIx) := by
  simp [canon]

theorem click_start_0_phase : (click start 0).phase = .oneOpen 0 := by
  simp [click, start, setFace]

theorem click_start_0_then_1_resolving :
    (click (click start 0) 1).phase = .resolving 0 1 := by
  simp [click, start, setFace, canon]

def afterPair0 : State := resolve (click (click start 0) 1)

theorem pair0_faces :
    ((afterPair0.cards 0).face = .matched) ∧
      ((afterPair0.cards 1).face = .matched) := by
  simp [afterPair0, resolve, click, start, setFace, setFace2, addScore, canon]

theorem pair0_score_p1 : afterPair0.score0 = 1 := by
  simp [afterPair0, resolve, click, start, setFace, setFace2, addScore, canon]

theorem pair0_score_p2 : afterPair0.score1 = 0 := by
  simp [afterPair0, resolve, click, start, setFace, setFace2, addScore, canon]

theorem pair0_turn_passed : afterPair0.turn = 1 := by
  simp [afterPair0, resolve, click, start, setFace, setFace2, addScore, canon,
        other]

theorem pair0_nMatched : afterPair0.nMatched = 1 := by
  simp [afterPair0, resolve, click, start, setFace, setFace2, addScore, canon]

theorem pair0_still_playing : afterPair0.outcome = .playing := by
  simp [afterPair0, resolve, click, start, setFace, setFace2, addScore, canon]

def afterMismatch : State := resolve (click (click start 0) 2)

theorem mismatch_closes :
    ((afterMismatch.cards 0).face = .down) ∧
      ((afterMismatch.cards 2).face = .down) := by
  simp [afterMismatch, resolve, click, start, setFace, setFace2, canon]

theorem mismatch_no_score :
    afterMismatch.score0 = 0 ∧ afterMismatch.score1 = 0 := by
  simp [afterMismatch, resolve, click, start, setFace, setFace2, canon]

theorem mismatch_turn_passed : afterMismatch.turn = 1 := by
  simp [afterMismatch, resolve, click, start, setFace, setFace2, canon, other]

theorem click_during_resolve_ignored :
    click (click (click start 0) 1) 3 = click (click start 0) 1 := by
  have h : (click (click start 0) 1).phase = .resolving 0 1 :=
    click_start_0_then_1_resolving
  exact click_ignored_resolving _ 0 1 3 h

end Ame.Memory
