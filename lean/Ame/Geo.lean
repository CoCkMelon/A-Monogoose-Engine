/-
  Geometry queries. Matches include/ame/geo.h without a solver or BVH.

  Memory picking is 2D: point-in-AABB on XY (cursor vs card rect).
  Ray hits are specified as "some natural t along origin + t • dir".
-/
namespace Ame.Geo

structure Aabb where
  minx : Int
  miny : Int
  minz : Int
  maxx : Int
  maxy : Int
  maxz : Int
  deriving DecidableEq, Repr

def make (cx cy cz hx hy hz : Int) : Aabb :=
  ⟨cx - hx, cy - hy, cz - hz, cx + hx, cy + hy, cz + hz⟩

def pointInXY (b : Aabb) (x y : Int) : Bool :=
  decide (b.minx ≤ x ∧ x ≤ b.maxx ∧ b.miny ≤ y ∧ y ≤ b.maxy)

def pointIn (b : Aabb) (x y z : Int) : Bool :=
  decide (b.minx ≤ x ∧ x ≤ b.maxx ∧
          b.miny ≤ y ∧ y ≤ b.maxy ∧
          b.minz ≤ z ∧ z ≤ b.maxz)

def overlap (a b : Aabb) : Bool :=
  decide (a.minx ≤ b.maxx ∧ a.maxx ≥ b.minx ∧
          a.miny ≤ b.maxy ∧ a.maxy ≥ b.miny ∧
          a.minz ≤ b.maxz ∧ a.maxz ≥ b.minz)

structure Ray where
  ox : Int
  oy : Int
  oz : Int
  dx : Int
  dy : Int
  dz : Int
  deriving Repr

def onRay (r : Ray) (t : Nat) : Int × Int × Int :=
  (r.ox + (t : Int) * r.dx,
   r.oy + (t : Int) * r.dy,
   r.oz + (t : Int) * r.dz)

/-- Exists a sample on the ray, t ≤ tmax, that lies in the box. -/
def rayHits (r : Ray) (b : Aabb) (tmax : Nat) : Prop :=
  ∃ t : Nat, t ≤ tmax ∧ pointIn b (onRay r t).1 (onRay r t).2.1 (onRay r t).2.2 = true

def card : Aabb := make 0 0 0 1 1 1

theorem centre_in_card : pointInXY card 0 0 = true := by decide

theorem far_miss_xy : pointInXY card 5 0 = false := by decide

theorem overlap_self : overlap card card = true := by decide

theorem overlap_apart :
    overlap card (make 8 0 0 1 1 1) = false := by decide

/-- Camera-style ray: from +Z down −Z onto the table. -/
def downZ : Ray := ⟨0, 0, 10, 0, 0, -1⟩

theorem downZ_hits_card : rayHits downZ card 20 := by
  refine ⟨10, by omega, by decide⟩

theorem downZ_misses_aside :
    ¬ rayHits ⟨5, 5, 10, 0, 0, -1⟩ card 20 := by
  rintro ⟨t, _ht, hin⟩
  simp [pointIn, onRay, card, make] at hin

/-- Squared-distance circle vs finite segment (C `ame_geo_circle_seg_xy`).
    Closest point via clamped projection; no roots. -/
def circleHitsSeg (cx cy r x0 y0 x1 y1 : Int) : Bool :=
  let dx := x1 - x0
  let dy := y1 - y0
  let l2 := dx * dx + dy * dy
  let ex := cx - x0
  let ey := cy - y0
  let num := ex * dx + ey * dy
  if l2 = 0 then
    decide (ex * ex + ey * ey ≤ r * r)
  else if num ≤ 0 then
    decide (ex * ex + ey * ey ≤ r * r)
  else if num ≥ l2 then
    let fx := cx - x1
    let fy := cy - y1
    decide (fx * fx + fy * fy ≤ r * r)
  else
    decide ((ex * ex + ey * ey) * l2 - num * num ≤ r * r * l2)

theorem circle_hits_ground :
    circleHitsSeg 0 1 2 (-4) 0 4 0 = true := by decide

theorem circle_misses_far :
    circleHitsSeg 0 10 1 (-4) 0 4 0 = false := by decide

theorem circle_hits_endpoint :
    circleHitsSeg 4 0 1 0 0 4 0 = true := by decide

theorem circle_misses_past_end :
    circleHitsSeg 6 0 1 0 0 4 0 = false := by decide

/-- Closest-point circle vs AABB on XY (C `ame_geo_circle_aabb_xy`). -/
def clampInt (x lo hi : Int) : Int :=
  if x < lo then lo else if x > hi then hi else x

def circleHitsAabb (cx cy r minx miny maxx maxy : Int) : Bool :=
  let px := clampInt cx minx maxx
  let py := clampInt cy miny maxy
  let dx := cx - px
  let dy := cy - py
  decide (dx * dx + dy * dy ≤ r * r)

theorem circle_aabb_top :
    circleHitsAabb 0 2 2 (-1) (-1) 1 1 = true := by decide

theorem circle_aabb_far :
    circleHitsAabb 0 10 1 (-1) (-1) 1 1 = false := by decide

theorem circle_aabb_inside :
    circleHitsAabb 0 0 1 (-2) (-2) 2 2 = true := by decide

theorem circle_aabb_corner :
    circleHitsAabb 2 2 2 (-1) (-1) 1 1 = true := by decide

end Ame.Geo
