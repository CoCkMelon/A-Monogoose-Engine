/-
Copyright (c) 2025 ame-next contributors.
Formal model of ame-next geometry: vectors and coordinate conventions
(pixel space, NDC) over the exact rationals. Pure Lean 4, no mathlib.

Mirrors include/ame/math.h (ame_v3_*) and the pixel/NDC conventions of
src/camera.c. Scalars are Rat: exact, so no rounding lemmas. Trig values
(tan(fov/2)) and normalizations (sqrt) are irrational in general - the
model takes them as parameters / states parallelism instead of length.
-/

namespace Ame

/-! ## Minimal rational-algebra prelude (core has the pieces, not ring) -/

theorem rat_neg_one_mul (x : Rat) : (-1 : Rat) * x = -x := by
  rw [Rat.neg_mul, Rat.one_mul]

theorem rat_neg_add (a b : Rat) : -(a + b) = -a + -b := by
  rw [← rat_neg_one_mul (a + b), Rat.mul_add, rat_neg_one_mul, rat_neg_one_mul]

theorem rat_mul_left_comm (a b c : Rat) : a * (b * c) = b * (a * c) := by
  rw [← Rat.mul_assoc, Rat.mul_comm a b, Rat.mul_assoc]

theorem rat_sub_eq (a b : Rat) : a - b = a + -b := Rat.sub_eq_add_neg a b

/-- The only genuinely numeral fact we need; everything else cancels
    algebraically. Kernel cannot reduce Rat ops (extern), so go through
    add_def + normalize_eq, where simp computes the Int/Nat parts. -/
theorem rat_one_add_one : (1 : Rat) + 1 = 2 := by
  simp [Rat.add_def, Rat.normalize_eq]

theorem rat_half_two : (2 : Rat) ≠ 0 := by simp

theorem rat_eq_of_mul_eq_mul_right {a b c : Rat} (hc : c ≠ 0)
    (h : a * c = b * c) : a = b := by
  calc a = a * c / c := (Rat.mul_div_cancel hc).symm
    _ = b * c / c := by rw [h]
    _ = b := Rat.mul_div_cancel hc

theorem rat_div_eq_of_cross {a b c d : Rat} (hb : b ≠ 0) (hd : d ≠ 0)
    (h : a * d = c * b) : a / b = c / d := by
  rw [Rat.div_def, Rat.div_def]
  have step1 : a * d * (d⁻¹ * b⁻¹) = a * b⁻¹ := by
    calc a * d * (d⁻¹ * b⁻¹) = a * (d * (d⁻¹ * b⁻¹)) := Rat.mul_assoc _ _ _
      _ = a * (d * d⁻¹ * b⁻¹) := by rw [← Rat.mul_assoc d d⁻¹ b⁻¹]
      _ = a * (1 * b⁻¹) := by rw [Rat.mul_inv_cancel d hd]
      _ = a * b⁻¹ := by rw [Rat.one_mul]
  have step2 : c * b * (d⁻¹ * b⁻¹) = c * d⁻¹ := by
    calc c * b * (d⁻¹ * b⁻¹) = c * (b * (d⁻¹ * b⁻¹)) := Rat.mul_assoc _ _ _
      _ = c * (d⁻¹ * (b * b⁻¹)) := by rw [rat_mul_left_comm b d⁻¹ b⁻¹]
      _ = c * (d⁻¹ * 1) := by rw [Rat.mul_inv_cancel b hb]
      _ = c * d⁻¹ := by rw [Rat.mul_one]
  calc a * b⁻¹ = a * d * (d⁻¹ * b⁻¹) := step1.symm
    _ = c * b * (d⁻¹ * b⁻¹) := by rw [h]
    _ = c * d⁻¹ := step2

theorem rat_eq_of_div_eq {a b c d : Rat} (hb : b ≠ 0) (hd : d ≠ 0)
    (h : a / b = c / d) : a * d = c * b := by
  rw [Rat.div_def, Rat.div_def] at h
  have step1 : a * b⁻¹ * (b * d) = a * d := by
    calc a * b⁻¹ * (b * d) = a * b⁻¹ * b * d := (Rat.mul_assoc _ _ _).symm
      _ = a * (b⁻¹ * b) * d := by rw [Rat.mul_assoc a b⁻¹ b]
      _ = a * 1 * d := by rw [Rat.inv_mul_cancel b hb]
      _ = a * d := by rw [Rat.mul_one]
  have step2 : c * d⁻¹ * (b * d) = c * b := by
    calc c * d⁻¹ * (b * d) = c * d⁻¹ * (d * b) := by rw [Rat.mul_comm b d]
      _ = c * d⁻¹ * d * b := (Rat.mul_assoc _ _ _).symm
      _ = c * (d⁻¹ * d) * b := by rw [Rat.mul_assoc c d⁻¹ d]
      _ = c * 1 * b := by rw [Rat.inv_mul_cancel d hd]
      _ = c * b := by rw [Rat.mul_one]
  calc a * d = a * b⁻¹ * (b * d) := step1.symm
    _ = c * d⁻¹ * (b * d) := by rw [h]
    _ = c * b := step2

theorem rat_mul_ne_zero {a b : Rat} (ha : a ≠ 0) (hb : b ≠ 0) : a * b ≠ 0 := by
  intro h
  apply ha
  calc a = a * (b * b⁻¹) := by rw [Rat.mul_inv_cancel b hb, Rat.mul_one]
    _ = (a * b) * b⁻¹ := by rw [← Rat.mul_assoc]
    _ = 0 * b⁻¹ := by rw [h]
    _ = 0 := Rat.zero_mul _

/-- `(a / b) * 2 * b = a * 2` (used by every px round-trip). -/
theorem rat_div_mul_two_mul (a b : Rat) (hb : b ≠ 0) :
    a / b * 2 * b = a * 2 := by
  calc a / b * 2 * b = a / b * (2 * b) := Rat.mul_assoc _ _ _
    _ = a / b * (b * 2) := by rw [Rat.mul_comm 2 b]
    _ = a / b * b * 2 := (Rat.mul_assoc _ _ _).symm
    _ = a * 2 := by rw [Rat.div_mul_cancel hb]

/-! ## Vectors (mirrors ame_v3 / ame_v4, Rat for exactness) -/

structure V3 where
  x : Rat
  y : Rat
  z : Rat
  deriving DecidableEq

structure V4 where
  x : Rat
  y : Rat
  z : Rat
  w : Rat
  deriving DecidableEq

def V3.zero : V3 := ⟨0, 0, 0⟩

def V3.add (a b : V3) : V3 := ⟨a.x + b.x, a.y + b.y, a.z + b.z⟩
/-- a - b, spelled additively so goals stay in + and * normal form. -/
def V3.sub (a b : V3) : V3 := ⟨a.x + -b.x, a.y + -b.y, a.z + -b.z⟩
def V3.scale (k : Rat) (a : V3) : V3 := ⟨k * a.x, k * a.y, k * a.z⟩

/-- ame_v3_dot -/
def V3.dot (a b : V3) : Rat := a.x * b.x + a.y * b.y + a.z * b.z

/-- ame_v3_cross -/
def V3.cross (a b : V3) : V3 :=
  ⟨a.y * b.z + -(a.z * b.y),
   a.z * b.x + -(a.x * b.z),
   a.x * b.y + -(a.y * b.x)⟩

instance : Add V3 := ⟨V3.add⟩

theorem V3.dot_comm (a b : V3) : a.dot b = b.dot a := by
  simp [V3.dot, Rat.mul_comm, Rat.add_assoc]

/-! ### Linearity of dot in the second argument (all we ever need) -/

theorem V3.dot_add (a b c : V3) : a.dot (b + c) = a.dot b + a.dot c := by
  show a.x * (b.x + c.x) + a.y * (b.y + c.y) + a.z * (b.z + c.z)
      = (a.x * b.x + a.y * b.y + a.z * b.z) + (a.x * c.x + a.y * c.y + a.z * c.z)
  simp [Rat.mul_add, Rat.add_assoc, Rat.add_left_comm]

theorem V3.dot_add_left (a b c : V3) : (a + b).dot c = a.dot c + b.dot c := by
  rw [V3.dot_comm, V3.dot_add, V3.dot_comm b c, V3.dot_comm a c]

theorem V3.dot_scale (a : V3) (k : Rat) (b : V3) :
    a.dot (V3.scale k b) = k * a.dot b := by
  simp [V3.dot, V3.scale, Rat.mul_add, rat_mul_left_comm, Rat.add_assoc]

theorem V3.dot_scale_left (k : Rat) (b : V3) (a : V3) :
    (V3.scale k b).dot a = k * b.dot a := by
  rw [V3.dot_comm, V3.dot_scale, V3.dot_comm]

theorem V3.dot_zero (a : V3) : a.dot V3.zero = 0 := by
  simp [V3.dot, V3.zero, Rat.mul_zero, Rat.add_zero]

/-! ### Reading coordinates off an orthonormal basis
The camera builds `f = norm(look - pos)`, `s = norm(cross f up)`,
`u = cross s f`; picking reasons about world points via their camera
coordinates `X Y D` where `P - pos = D*f + X*s + Y*u`. -/

/-- The D (forward) coordinate of the decomposition. -/
theorem dot_coords_f {f s u p P : V3} {X Y D : Rat}
    (hV : P.sub p = (V3.scale D f + V3.scale X s) + V3.scale Y u)
    (hf : f.dot f = 1) (hsf : s.dot f = 0) (huf : u.dot f = 0) :
    (P.sub p).dot f = D := by
  rw [hV, V3.dot_add_left, V3.dot_add_left, V3.dot_scale_left, V3.dot_scale_left,
      V3.dot_scale_left,
      hsf, huf, hf, Rat.mul_one, Rat.mul_zero, Rat.mul_zero, Rat.add_zero,
      Rat.add_zero]

/-- The X (right) coordinate of the decomposition. -/
theorem dot_coords_s {f s u p P : V3} {X Y D : Rat}
    (hV : P.sub p = (V3.scale D f + V3.scale X s) + V3.scale Y u)
    (hs : s.dot s = 1) (hfs : f.dot s = 0) (hus : u.dot s = 0) :
    (P.sub p).dot s = X := by
  rw [hV, V3.dot_add_left, V3.dot_add_left, V3.dot_scale_left, V3.dot_scale_left,
      V3.dot_scale_left,
      hfs, hus, hs, Rat.mul_one, Rat.mul_zero, Rat.mul_zero, Rat.add_zero,
      Rat.zero_add]

/-- The Y (up) coordinate of the decomposition. -/
theorem dot_coords_u {f s u p P : V3} {X Y D : Rat}
    (hV : P.sub p = (V3.scale D f + V3.scale X s) + V3.scale Y u)
    (hu : u.dot u = 1) (hfu : f.dot u = 0) (hsu : s.dot u = 0) :
    (P.sub p).dot u = Y := by
  rw [hV, V3.dot_add_left, V3.dot_add_left, V3.dot_scale_left, V3.dot_scale_left,
      V3.dot_scale_left,
      hfu, hsu, hu, Rat.mul_one, Rat.mul_zero, Rat.mul_zero, Rat.add_zero,
      Rat.zero_add]

/-! ## Pixel <-> NDC conventions (mirror src/camera.c)

px space: origin top-left, y DOWN, units = pixels.
NDC: x right, y UP, range [-1, 1]. The y axis flips between them. -/

/-- camera_screen_ray: `nx = (sx / vw) * 2 - 1` -/
def ndcOfPxX (sx vw : Rat) : Rat := sx / vw * 2 - 1
/-- camera_screen_ray: `ny = 1 - (sy / vh) * 2` -/
def ndcOfPxY (sy vh : Rat) : Rat := 1 - sy / vh * 2

/-- GPU/test viewport: `sx = (nx / 2 + 1/2) * vw` after the perspective
divide (test_camera.c proj()). -/
def pxOfNdcX (nx vw : Rat) : Rat := (nx / 2 + 1 / 2) * vw
/-- `sy = (1 - (ny / 2 + 1/2)) * vh` (y flip back). -/
def pxOfNdcY (ny vh : Rat) : Rat := (1 - (ny / 2 + 1 / 2)) * vh

private theorem half_mul_two (a : Rat) : a / 2 * 2 = a :=
  Rat.div_mul_cancel rat_half_two

/-- `(a / 2 + 1/2) * 2 = a + 1` -/
theorem rat_half_add_half_two (a : Rat) : (a / 2 + 1 / 2) * 2 = a + 1 := by
  rw [Rat.add_mul, half_mul_two, half_mul_two]

/-- `(1 - t) * 2 = 2 - t * 2` -/
theorem rat_one_sub_mul_two (t : Rat) : (1 - t) * 2 = 2 + -(t * 2) := by
  rw [rat_sub_eq, Rat.add_mul, Rat.one_mul, Rat.neg_mul]

theorem ndcOfPxX_pxOfNdcX (nx : Rat) {vw : Rat} (hvw : vw ≠ 0) :
    ndcOfPxX (pxOfNdcX nx vw) vw = nx := by
  show (nx / 2 + 1 / 2) * vw / vw * 2 - 1 = nx
  rw [Rat.mul_div_cancel hvw, rat_half_add_half_two, rat_sub_eq]
  have h11 : (1 : Rat) + -1 = 0 := Rat.add_neg_cancel 1
  calc nx + 1 + -1 = nx + (1 + -1) := Rat.add_assoc nx 1 (-1)
    _ = nx + 0 := by rw [h11]
    _ = nx := Rat.add_zero nx

theorem pxOfNdcX_ndcOfPxX (sx : Rat) {vw : Rat} (hvw : vw ≠ 0) :
    pxOfNdcX (ndcOfPxX sx vw) vw = sx := by
  have h11 : (-1 : Rat) + 1 = 0 := Rat.neg_add_cancel 1
  have key : (((sx / vw * 2 - 1) / 2 + 1 / 2) * 2) * vw = sx * 2 := by
    calc ((sx / vw * 2 - 1) / 2 + 1 / 2) * 2 * vw
        = ((sx / vw * 2 - 1) + 1) * vw := by rw [rat_half_add_half_two]
      _ = (sx / vw * 2 + -1 + 1) * vw := by rw [rat_sub_eq]
      _ = (sx / vw * 2 + (-1 + 1)) * vw := by rw [Rat.add_assoc]
      _ = sx / vw * 2 * vw := by rw [h11, Rat.add_zero]
      _ = sx * 2 := rat_div_mul_two_mul sx vw hvw
  exact rat_eq_of_mul_eq_mul_right rat_half_two
    (by show ((sx / vw * 2 - 1) / 2 + 1 / 2) * vw * 2 = sx * 2
        rw [Rat.mul_assoc, Rat.mul_comm vw 2, ← Rat.mul_assoc]
        exact key)

theorem ndcOfPxY_pxOfNdcY (ny : Rat) {vh : Rat} (hvh : vh ≠ 0) :
    ndcOfPxY (pxOfNdcY ny vh) vh = ny := by
  show 1 - (1 - (ny / 2 + 1 / 2)) * vh / vh * 2 = ny
  rw [Rat.mul_div_cancel hvh, rat_one_sub_mul_two, rat_half_add_half_two,
      rat_sub_eq, rat_neg_add, Rat.neg_neg]
  -- 1 + (-2 + (ny + 1)) = ny
  have h3 : (1 : Rat) + 1 + -2 = 0 := by
    rw [rat_one_add_one, Rat.add_neg_cancel]
  calc 1 + (-2 + (ny + 1)) = 1 + (-2 + (1 + ny)) := by rw [Rat.add_comm ny 1]
    _ = 1 + (1 + (-2 + ny)) := by rw [Rat.add_left_comm (-2) 1 ny]
    _ = (1 + 1) + (-2 + ny) := (Rat.add_assoc 1 1 (-2 + ny)).symm
    _ = ((1 + 1) + -2) + ny := (Rat.add_assoc (1 + 1) (-2) ny).symm
    _ = 0 + ny := by rw [h3]
    _ = ny := Rat.zero_add ny

theorem pxOfNdcY_ndcOfPxY (sy : Rat) {vh : Rat} (hvh : vh ≠ 0) :
    pxOfNdcY (ndcOfPxY sy vh) vh = sy := by
  have q2 : sy / vh * 2 * vh = sy * 2 := rat_div_mul_two_mul sy vh hvh
  have h11 : (-1 : Rat) + 1 = 0 := Rat.neg_add_cancel 1
  have h22 : (2 : Rat) + -2 = 0 := Rat.add_neg_cancel 2
  have key : (1 - ((1 - sy / vh * 2) / 2 + 1 / 2)) * 2 * vh = sy * 2 := by
    have sq : (1 - sy / vh * 2) + 1 = 2 + -(sy / vh * 2) := by
      have h12 : (1 : Rat) + 1 = 2 := rat_one_add_one
      rw [rat_sub_eq]
      calc (1 + -(sy / vh * 2)) + 1
          = 1 + (-(sy / vh * 2) + 1) := Rat.add_assoc 1 _ 1
        _ = 1 + (1 + -(sy / vh * 2)) := by rw [Rat.add_comm (-(sy / vh * 2)) 1]
        _ = (1 + 1) + -(sy / vh * 2) := (Rat.add_assoc 1 1 _).symm
        _ = 2 + -(sy / vh * 2) := by rw [h12]
    calc (1 - ((1 - sy / vh * 2) / 2 + 1 / 2)) * 2 * vh
        = (2 + -(((1 - sy / vh * 2) / 2 + 1 / 2) * 2)) * vh := by
          rw [rat_one_sub_mul_two]
      _ = (2 + -((1 - sy / vh * 2) + 1)) * vh := by rw [rat_half_add_half_two]
      _ = (2 + -(2 + -(sy / vh * 2))) * vh := by rw [sq]
      _ = (2 + (-2 + sy / vh * 2)) * vh := by rw [rat_neg_add, Rat.neg_neg]
      _ = (2 + -2 + sy / vh * 2) * vh := by rw [← Rat.add_assoc]
      _ = sy / vh * 2 * vh := by rw [h22, Rat.zero_add]
      _ = sy * 2 := q2
  exact rat_eq_of_mul_eq_mul_right rat_half_two
    (by show (1 - ((1 - sy / vh * 2) / 2 + 1 / 2)) * vh * 2 = sy * 2
        rw [Rat.mul_assoc, Rat.mul_comm vh 2, ← Rat.mul_assoc]
        exact key)

end Ame
