/-
Copyright (c) 2025 ame-next contributors.
Formal model of src/camera.c queries: screen -> ray (picking) and the
2D screen -> world mapping. Pure Lean 4, no mathlib.

The 3D camera basis is f = norm(look-pos), s = norm(cross f up),
u = cross s f, taken as arguments with orthonormality as hypotheses
(sqrt is not rational; see Ame.Geo for the convention notes).
-/
import Ame.Geo
import Ame.M4

namespace Ame.Camera

/-! ## Scalar helpers -/

/-- `D * (q / D) = q` for `D ≠ 0` (division cancels left). -/
theorem rat_mul_div_cancel (D : Rat) (hD : D ≠ 0) (q : Rat) : D * (q / D) = q := by
  rw [Rat.div_def]
  calc D * (q * D⁻¹) = D * q * D⁻¹ := (Rat.mul_assoc _ _ _).symm
    _ = q * D * D⁻¹ := by rw [Rat.mul_comm D q]
    _ = q * (D * D⁻¹) := Rat.mul_assoc _ _ _
    _ = q * 1 := by rw [Rat.mul_inv_cancel D hD]
    _ = q := Rat.mul_one q

/-- `a / (u * D) * u = a / D`: the tan*aspect factor cancels, leaving
the depth division. This is the algebra behind "nx * t * a = X / D". -/
theorem rat_div_mul_mul_cancel (a u D : Rat) (hu : u ≠ 0) (hD : D ≠ 0) :
    a / (u * D) * u = a / D := by
  rw [Rat.div_def]
  have step : a * (u * D)⁻¹ * u = a * u * (u * D)⁻¹ := by
    rw [Rat.mul_assoc, Rat.mul_comm (u * D)⁻¹ u, ← Rat.mul_assoc]
  rw [step, ← Rat.div_def]
  exact rat_div_eq_of_cross (rat_mul_ne_zero hu hD) hD (by rw [Rat.mul_assoc a u D])

/-! ## Screen -> ray (mirror of camera_screen_ray, perspective branch) -/

/-- The RAW (unnormalized) pick direction of camera_screen_ray:
    `d = f + s*(nx*t*a) + u*(ny*t)` with t = tan(fov/2), a = aspect.
    The C normalizes d before use; normalization does not change which
    points the ray passes through, and sqrt is not rational, so the
    model works with the raw direction and exact hit parameters. -/
def rayRaw (f s u : V3) (nx ny t a : Rat) : V3 :=
  V3.add f (V3.add (V3.scale (nx * t * a) s) (V3.scale (ny * t) u))

/-- p + (P - p) = P. -/
theorem add_sub_self (p P : V3) : p + P.sub p = P := by
  show ⟨p.x + (P.x + -p.x), p.y + (P.y + -p.y), p.z + (P.z + -p.z)⟩ = P
  have h : ∀ a b : Rat, a + (b + -a) = b := by
    intro a b
    rw [Rat.add_left_comm a b (-a), Rat.add_neg_cancel a, Rat.add_zero b]
  simp [h]

/-- PICKING SOUNDNESS (perspective): if the pixel (sx, sy) maps to NDC
    (nx, ny) with nx = X/(t*a*D) and ny = Y/(t*D) - i.e. the pixel the
    camera projects P to, where `P - pos = D*f + X*s + Y*u` - then the
    pick ray passes EXACTLY through P, at ray parameter k = D. -/
theorem ray_hits_sub {f s u p P : V3} {X Y D t a nx ny : Rat}
    (hV : P.sub p = V3.add (V3.add (V3.scale D f) (V3.scale X s))
                         (V3.scale Y u))
    (hnx : nx = X / (t * a * D)) (hny : ny = Y / (t * D))
    (ht : t ≠ 0) (ha : a ≠ 0) (hD : D ≠ 0) :
    V3.scale D (rayRaw f s u nx ny t a) = P.sub p := by
  have hta : t * a ≠ 0 := rat_mul_ne_zero ht ha
  have c1 : X / (t * a * D) * (t * a) = X / D :=
    rat_div_mul_mul_cancel X (t * a) D hta hD
  have c2 : Y / (t * D) * t = Y / D := rat_div_mul_mul_cancel Y t D ht hD
  rw [hV]
  show V3.scale D (V3.add f (V3.add (V3.scale (nx * t * a) s)
      (V3.scale (ny * t) u)))
      = V3.add (V3.add (V3.scale D f) (V3.scale X s)) (V3.scale Y u)
  rw [hnx, hny, Rat.mul_assoc (X / (t * a * D)) t a, c1, c2]
  have keymul : ∀ q r : Rat, D * (q / D * r) = q * r := by
    intro q r
    rw [← Rat.mul_assoc, rat_mul_div_cancel D hD q]
  simp [V3.add, V3.scale, Rat.mul_add, Rat.add_assoc, Rat.add_comm,
        Rat.add_left_comm, keymul]

/-- p + ray(k=D) = P: the form used by hit testing in mem_app.c. -/
theorem ray_hits {f s u p P : V3} {X Y D t a nx ny : Rat}
    (hV : P.sub p = V3.add (V3.add (V3.scale D f) (V3.scale X s))
                         (V3.scale Y u))
    (hnx : nx = X / (t * a * D)) (hny : ny = Y / (t * D))
    (ht : t ≠ 0) (ha : a ≠ 0) (hD : D ≠ 0) :
    p + V3.scale D (rayRaw f s u nx ny t a) = P := by
  rw [ray_hits_sub hV hnx hny ht ha hD]
  exact add_sub_self p P

end Ame.Camera
