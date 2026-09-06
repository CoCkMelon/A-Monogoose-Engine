/- Ame.Text — the text GRID contract (mirror of src/text.c pen_snap model).

The bug this file forbids: glyph quads rasterize from one coordinate
stream (the pen, accumulated FLOAT advances) while carets/selection
read another (independently rounded), so each primitive snaps to its
own pixel and the caret lands visibly off the ink - the text editor
showed exactly this (inherited from master's metrics-vs-renderer
split), and fractional pens blurred every glyph.

The fix modeled here: ONE snapped pen function. Layout accumulates
exact Rat advances but STORES floor(pen + 1/2); draw, caret,
selection and hit-testing all consume that grid. Then:

  - caret i = snap (pen i)            (definition, not a rounding)
  - ink i   = snap (pen i) + xoff i   (bearing applied on the grid)
  - EOL     = caret n = snap (pen n) = width   (no +2px fudges)

so the caret-vs-ink offset is EXACTLY the bearing, and alignment
shifts / measured width stay integers. -/

namespace Ame.Text

/-- floor(x + 1/2) - the exact mirror of pen_snap() in src/text.c. -/
def snap (x : Rat) : Int := (x + 1 / 2).floor

/-- A font run: exact advances (stb gives e.g. 17.375) and per-glyph
    left bearings (integers in the baked table). -/
structure Font where
  adv : List Rat
  xoff : List Int
deriving Repr

/-- Exact pen before glyph i: the sum of the first i advances -
    exactly what C accumulates in pen_x. -/
def pen (f : Font) (i : Nat) : Rat := (f.adv.take i).sum

/-- Caret before glyph i reads the grid - ON it by definition. -/
def caretX (f : Font) (i : Nat) : Int := snap (pen f i)

/-- Ink left edge of glyph i: grid pen + integer bearing. -/
def inkX (f : Font) (i : Nat) : Int := caretX f i + f.xoff.getD i 0

/-- Measured width: the snapped final pen (out->w in C). -/
def width (f : Font) : Int := snap (pen f f.adv.length)

/-- THE alignment law: the caret-vs-ink horizontal offset is EXACTLY
    the bearing - no rounding term can appear between them. The old
    bug class (caret on a float pen, ink on a snapped pen) breaks the
    left side; on the grid contract the proof is trivial, which is
    the point: the model makes the bug unstateable. -/
theorem caret_ink_exact (f : Font) (i : Nat) :
    inkX f i - caretX f i = f.xoff.getD i 0 := by
  simp [inkX]
  omega

/-- The EOL caret and the reported width are the SAME grid point. -/
theorem eol_caret_eq_width (f : Font) :
    caretX f f.adv.length = width f := rfl

/-- Alignment (center/right) shifts the WHOLE line by one integer:
    grid gaps between neighboring glyphs are invariant - the string
    is translated, never sheared. -/
theorem align_shift_preserves_gaps (shift : Int) (g : List Int) :
    ∀ i, (i + 1 < g.length) →
      (g.getD (i + 1) 0 + shift) - (g.getD i 0 + shift)
        = g.getD (i + 1) 0 - g.getD i 0 := by
  intro i _
  omega

/-- Concrete rig: fractional advances like the real baked table
    (17, 8.25, 19.9, 9.1) with mixed bearings. -/
def rig : Font where
  adv := [17, 33/4, 199/10, 91/10]
  xoff := [1, 0, 2, 1]

/-- Bounds of snap on the rig (pure core has no linarith, so the
    Rat bounds are closed propositions checked by native_decide -
    the house pattern). Every grid slot, INCLUDING the EOL width,
    sits within half a pixel of the exact pen. -/
def rigSlots : List (Rat × Int) :=
  (List.range (rig.adv.length + 1)).map (fun i => (pen rig i, snap (pen rig i)))

theorem rig_snap_err :
    rigSlots.all (fun (p, s) =>
      p - 1 / 2 ≤ (s : Rat) ∧ (s : Rat) ≤ p + 1 / 2) = true := by
  native_decide

/-- Rig: the caret sits exactly bearing-left of the ink for every
    glyph (the general law above instantiated and closed). -/
theorem rig_caret_ink :
    ((List.range rig.adv.length).all (fun i =>
      inkX rig i - caretX rig i = rig.xoff.getD i 0)) = true := by
  native_decide

/-! ### Tag runs and the glyph cap (audit s22)

The two C bug classes below were found by the caret-coordinates
audit; both are now mirrored here so they cannot be RE-stated on
the model.

P1 tag desync: `{c=FF0000}abc` pinned the caret at the tag's byte
offset. In the model, a tagged run interleaves real cells with TAG
cells, and a tag cell contributes NO advance - so the pen of the
k-th real glyph is the same number with or without the tags. On
this model "the tag moved the caret" is not even expressible.

P2 phantom width: a 512-glyph cap reported w as if the 600th glyph
had been reached. The clamped width is the pen AT the cap - by
definition, not by convention. -/

/-- A tagged run: each cell is an advance plus "is a real glyph"
    (tag cells carry `false` - they recolour, never advance). -/
def TaggedRun := List (Rat × Bool)

/-- Exact pen of the k-th real glyph in a tagged run. -/
def realPen : TaggedRun → Nat → Rat
  | [], _ => 0
  | (_, true) :: _, 0 => 0
  | (a, true) :: rest, k + 1 => a + realPen rest k
  | (_, false) :: rest, k => realPen rest k

/-- pen_insert: tags never move the pen. The pen of the k-th real
    cell equals the pen of the k-th cell of the STRIPPED run - i.e.
    `text_layout("{c=FF0000}abc")` and `text_layout_plain("abc")`
    share every grid slot. This is the theorem ci.yml audits. -/
theorem pen_insert (cells : TaggedRun) (k : Nat) :
    realPen cells k
      = pen { adv := (cells.filter (·.2)).map (·.1), xoff := [] } k := by
  induction cells generalizing k with
  | nil => simp [realPen, pen]
  | cons c rest ih =>
    obtain ⟨a, isReal⟩ := c
    cases h2 : isReal <;> cases k <;>
      simp_all [realPen, pen, List.take, List.filter]

/-- Glyph-cap clamp: stopping after k stored elements reports the
    pen AT k. A width leaking past the cap (the 600-'m' w=12270
    phantom) contradicts this by rfl. -/
theorem cap_width_eq_pen (f : Font) (k : Nat)
    (h : k ≤ f.adv.length) :
    width { adv := f.adv.take k, xoff := f.xoff.take k }
      = snap (pen f k) := by
  simp [width, pen, List.take_take, Nat.min_eq_left h]

/-- Rig for the tag law: "{c=FF0000}ab{/c}c" as tagged cells with
    the two tag cells marked false. -/
def rigTags : TaggedRun :=
  [(0, false), (17, true), (33/4, true), (0, false), (199/10, true)]

/-- Closed instance: stripping the tags yields exactly the untagged
    rig's pen at every real index (0, 1, 2) and at EOL. -/
theorem rig_pen_insert :
    ([0, 1, 2, 3].all (fun k =>
      realPen rigTags k
        = pen { adv := (rigTags.filter (·.2)).map (·.1), xoff := [] } k))
      = true := by
  native_decide

end Ame.Text
