/-
Copyright (c) 2025 ame-next contributors.
Formal model of the vertex-shader path in src/render.c:
`gl_Position = u_vp * vec4(a_pos, 1.0)` followed by the GPU perspective
divide + viewport (mirrored by proj() in tests/test_camera.c), and the
AGREEMENT theorems: for a perspective camera the shader path produces
the same pixel as the analytic camera model that camera_screen_ray
uses. That agreement is what makes picking exactness
(Ame.Camera.ray_hits) apply to what the GPU actually draws.
Pure Lean 4, no mathlib.
-/
import Ame.Geo
import Ame.M4
import Ame.Camera

namespace Ame.Shader

/-- `gl_Position = u_vp * vec4(a_pos, 1.0)` - the vertex shader body. -/
def vertShader (u_vp : Mat4) (a_pos : V3) : V4 :=
  u_vp.mulv ⟨a_pos.x, a_pos.y, a_pos.z, 1⟩

/-- GPU fixed function after the shader: perspective divide, then the
    viewport transform (px origin top-left, y down). -/
def gpuViewport (vw vh : Rat) (clip : V4) : Rat × Rat :=
  (pxOfNdcX (clip.x / clip.w) vw, pxOfNdcY (clip.y / clip.w) vh)

/-- The u_vp of the 3D camera: perspective after look-at. -/
def vp3 (t a zn zf : Rat) (f s u eye : V3) : Mat4 :=
  (Mat4.persp t a zn zf).mul (Mat4.lookAt f s u eye)

/-! ## Matrix algebra -/

set_option maxHeartbeats 4000000 in
/-- Matrix-vector through a product: applying a*b is b first, then a. -/
theorem mulv_mul (a b : Mat4) (v : V4) :
    (a.mul b).mulv v = a.mulv (b.mulv v) := by
  simp [Mat4.mul, Mat4.mulv, V4.add, V4.scale, Rat.mul_add, Rat.add_mul,
        Rat.mul_assoc, Rat.mul_comm, rat_mul_left_comm, Rat.add_assoc,
        Rat.add_comm, Rat.add_left_comm]

/-- dot is subtractive in the second argument. -/
theorem dot_sub (a b c : V3) : a.dot (b.sub c) = a.dot b + -(a.dot c) := by
  simp [V3.dot, V3.sub, Rat.mul_add, Rat.mul_neg, rat_neg_add, Rat.add_assoc,
        Rat.add_comm, Rat.add_left_comm]

private theorem dot_zero_of_symm {a b : V3} (h : a.dot b = 0) : b.dot a = 0 := by
  rw [V3.dot_comm]; exact h

/-! ## The look-at stage sends P to its camera coordinates -/

/-- lookAt-row s of the view matrix computes the X camera coordinate. -/
theorem lookAt_x (f s u eye P : V3) (X Y D : Rat)
    (hV : P.sub eye = V3.add (V3.add (V3.scale D f) (V3.scale X s))
                             (V3.scale Y u))
    (hs : s.dot s = 1) (hfs : f.dot s = 0) (hsu : s.dot u = 0) :
    ((Mat4.lookAt f s u eye).mulv ⟨P.x, P.y, P.z, 1⟩).x = X := by
  have hcoord : s.dot (P.sub eye) = X := by
    rw [V3.dot_comm]
    exact Ame.dot_coords_s hV hs hfs (dot_zero_of_symm hsu)
  show P.x * s.x + (P.y * s.y + (P.z * s.z + 1 * -(s.dot eye))) = X
  have hsplit : ∀ q : Rat,
      P.x * s.x + (P.y * s.y + (P.z * s.z + q)) = s.dot P + q := by
    intro q
    simp [V3.dot, Rat.mul_comm, rat_mul_left_comm, Rat.mul_assoc,
          Rat.add_assoc, Rat.add_left_comm]
  rw [Rat.one_mul, hsplit (-(s.dot eye)), ← dot_sub s P eye]
  exact hcoord

/-- lookAt-row u of the view matrix computes the Y camera coordinate. -/
theorem lookAt_y (f s u eye P : V3) (X Y D : Rat)
    (hV : P.sub eye = V3.add (V3.add (V3.scale D f) (V3.scale X s))
                             (V3.scale Y u))
    (hu : u.dot u = 1) (hfu : f.dot u = 0) (hsu : s.dot u = 0) :
    ((Mat4.lookAt f s u eye).mulv ⟨P.x, P.y, P.z, 1⟩).y = Y := by
  have hcoord : u.dot (P.sub eye) = Y := by
    rw [V3.dot_comm]
    exact Ame.dot_coords_u hV hu hfu hsu
  show P.x * u.x + (P.y * u.y + (P.z * u.z + 1 * -(u.dot eye))) = Y
  have hsplit : ∀ q : Rat,
      P.x * u.x + (P.y * u.y + (P.z * u.z + q)) = u.dot P + q := by
    intro q
    simp [V3.dot, Rat.mul_comm, rat_mul_left_comm, Rat.mul_assoc,
          Rat.add_assoc, Rat.add_left_comm]
  rw [Rat.one_mul, hsplit (-(u.dot eye)), ← dot_sub u P eye]
  exact hcoord

/-- lookAt-row -f computes the negative depth: eye-space z = -D. -/
theorem lookAt_z (f s u eye P : V3) (X Y D : Rat)
    (hV : P.sub eye = V3.add (V3.add (V3.scale D f) (V3.scale X s))
                             (V3.scale Y u))
    (hf : f.dot f = 1) (hfs : f.dot s = 0) (hfu : f.dot u = 0) :
    ((Mat4.lookAt f s u eye).mulv ⟨P.x, P.y, P.z, 1⟩).z = -D := by
  have hcoord : f.dot (P.sub eye) = D := by
    rw [V3.dot_comm]
    exact Ame.dot_coords_f hV hf (dot_zero_of_symm hfs) (dot_zero_of_symm hfu)
  show P.x * -f.x + (P.y * -f.y + (P.z * -f.z + 1 * f.dot eye)) = -D
  have hsplit : P.x * f.x + (P.y * f.y + P.z * f.z) = f.dot P := by
    simp [V3.dot, Rat.mul_comm, rat_mul_left_comm, Rat.mul_assoc,
          Rat.add_assoc, Rat.add_left_comm]
  rw [Rat.one_mul]
  have hneg : P.x * -f.x + (P.y * -f.y + (P.z * -f.z + f.dot eye))
      = -((P.x * f.x + (P.y * f.y + P.z * f.z)) + -(f.dot eye)) := by
    simp [rat_neg_add, Rat.mul_neg, Rat.neg_mul, Rat.neg_neg, Rat.add_assoc,
          Rat.add_comm, Rat.add_left_comm]
  rw [hneg, hsplit, ← dot_sub f P eye, hcoord]

theorem lookAt_w (f s u eye P : V3) :
    ((Mat4.lookAt f s u eye).mulv ⟨P.x, P.y, P.z, 1⟩).w = 1 := by
  show P.x * 0 + (P.y * 0 + (P.z * 0 + 1 * 1)) = 1
  rw [Rat.mul_zero, Rat.mul_zero, Rat.mul_zero, Rat.zero_add, Rat.zero_add,
      Rat.zero_add, Rat.mul_one]

/-- The look-at stage: a world point with camera coordinates (X, Y, D)
    lands exactly on the eye-space point (X, Y, -D, 1). -/
theorem lookAt_vec (f s u eye P : V3) (X Y D : Rat)
    (hV : P.sub eye = V3.add (V3.add (V3.scale D f) (V3.scale X s))
                             (V3.scale Y u))
    (hf : f.dot f = 1) (hs : s.dot s = 1) (hu : u.dot u = 1)
    (hfs : f.dot s = 0) (hfu : f.dot u = 0) (hsu : s.dot u = 0) :
    (Mat4.lookAt f s u eye).mulv ⟨P.x, P.y, P.z, 1⟩ = ⟨X, Y, -D, 1⟩ := by
  show V4.mk (((Mat4.lookAt f s u eye).mulv ⟨P.x, P.y, P.z, 1⟩).x)
        (((Mat4.lookAt f s u eye).mulv ⟨P.x, P.y, P.z, 1⟩).y)
        (((Mat4.lookAt f s u eye).mulv ⟨P.x, P.y, P.z, 1⟩).z)
        (((Mat4.lookAt f s u eye).mulv ⟨P.x, P.y, P.z, 1⟩).w)
      = ⟨X, Y, -D, 1⟩
  rw [lookAt_x f s u eye P X Y D hV hs hfs hsu,
      lookAt_y f s u eye P X Y D hV hu hfu hsu,
      lookAt_z f s u eye P X Y D hV hf hfs hfu, lookAt_w]

/-! ## The perspective stage -/

theorem persp_x (t a zn zf X Y D : Rat) :
    ((Mat4.persp t a zn zf).mulv ⟨X, Y, -D, 1⟩).x = (1 / (t * a)) * X := by
  show X * (1 / (t * a)) + (Y * 0 + (-D * 0 + 1 * 0)) = (1 / (t * a)) * X
  rw [Rat.mul_zero, Rat.mul_zero, Rat.mul_zero, Rat.add_zero, Rat.add_zero,
      Rat.add_zero, Rat.mul_comm]

theorem persp_y (t a zn zf X Y D : Rat) :
    ((Mat4.persp t a zn zf).mulv ⟨X, Y, -D, 1⟩).y = (1 / t) * Y := by
  show X * 0 + (Y * (1 / t) + (-D * 0 + 1 * 0)) = (1 / t) * Y
  rw [Rat.mul_zero, Rat.mul_zero, Rat.mul_zero, Rat.zero_add, Rat.add_zero,
      Rat.add_zero, Rat.mul_comm]

theorem persp_w (t a zn zf X Y D : Rat) :
    ((Mat4.persp t a zn zf).mulv ⟨X, Y, -D, 1⟩).w = D := by
  show X * 0 + (Y * 0 + (-D * -1 + 1 * 0)) = D
  rw [Rat.mul_zero, Rat.mul_zero, Rat.mul_zero, Rat.zero_add, Rat.zero_add,
      Rat.add_zero]
  rw [Rat.neg_mul, Rat.mul_neg, Rat.neg_neg, Rat.mul_one]

/-! ## Division plumbing -/

theorem one_div_mul (q u : Rat) : (1 / u) * q = q / u := by
  rw [Rat.div_def, Rat.div_def, Rat.one_mul, Rat.mul_comm]

/-- `(1/u)*q / D = q / (u*D)`: the shader's clip x over clip w is the
    analytic NDC x. -/
theorem div_div (q u D : Rat) (hu : u ≠ 0) (hD : D ≠ 0) :
    (1 / u) * q / D = q / (u * D) := by
  rw [one_div_mul]
  exact rat_div_eq_of_cross hD (rat_mul_ne_zero hu hD)
    (by rw [← Rat.mul_assoc, Rat.div_mul_cancel hu])

/-! ## Agreement: shader pixel == analytic camera pixel -/

/-- AGREEMENT, x: the GPU viewport x of the shader output equals the
    analytic pixel for camera coordinate X. -/
theorem shader_pixel_x (f s u eye P : V3) (X Y D t a vw vh zn zf : Rat)
    (hV : P.sub eye = V3.add (V3.add (V3.scale D f) (V3.scale X s))
                             (V3.scale Y u))
    (hf : f.dot f = 1) (hs : s.dot s = 1) (hu : u.dot u = 1)
    (hfs : f.dot s = 0) (hfu : f.dot u = 0) (hsu : s.dot u = 0)
    (ht : t ≠ 0) (ha : a ≠ 0) (hD : D ≠ 0) :
    (gpuViewport vw vh (vertShader (vp3 t a zn zf f s u eye) P)).1
      = pxOfNdcX (X / (t * a * D)) vw := by
  have hlook : (Mat4.lookAt f s u eye).mulv ⟨P.x, P.y, P.z, 1⟩
      = ⟨X, Y, -D, 1⟩ :=
    lookAt_vec f s u eye P X Y D hV hf hs hu hfs hfu hsu
  have hx : (vertShader (vp3 t a zn zf f s u eye) P).x
      = (1 / (t * a)) * X := by
    show ((vp3 t a zn zf f s u eye).mulv ⟨P.x, P.y, P.z, 1⟩).x
        = (1 / (t * a)) * X
    simp only [vp3]
    rw [mulv_mul, hlook]
    exact persp_x t a zn zf X Y D
  have hw : (vertShader (vp3 t a zn zf f s u eye) P).w = D := by
    show ((vp3 t a zn zf f s u eye).mulv ⟨P.x, P.y, P.z, 1⟩).w = D
    simp only [vp3]
    rw [mulv_mul, hlook]
    exact persp_w t a zn zf X Y D
  show pxOfNdcX
      ((vertShader (vp3 t a zn zf f s u eye) P).x
        / (vertShader (vp3 t a zn zf f s u eye) P).w) vw
    = pxOfNdcX (X / (t * a * D)) vw
  rw [hx, hw, div_div X (t * a) D (rat_mul_ne_zero ht ha) hD]

/-- AGREEMENT, y: the GPU viewport y equals the analytic pixel for Y. -/
theorem shader_pixel_y (f s u eye P : V3) (X Y D t a vw vh zn zf : Rat)
    (hV : P.sub eye = V3.add (V3.add (V3.scale D f) (V3.scale X s))
                             (V3.scale Y u))
    (hf : f.dot f = 1) (hs : s.dot s = 1) (hu : u.dot u = 1)
    (hfs : f.dot s = 0) (hfu : f.dot u = 0) (hsu : s.dot u = 0)
    (ht : t ≠ 0) (ha : a ≠ 0) (hD : D ≠ 0) :
    (gpuViewport vw vh (vertShader (vp3 t a zn zf f s u eye) P)).2
      = pxOfNdcY (Y / (t * D)) vh := by
  have hlook : (Mat4.lookAt f s u eye).mulv ⟨P.x, P.y, P.z, 1⟩
      = ⟨X, Y, -D, 1⟩ :=
    lookAt_vec f s u eye P X Y D hV hf hs hu hfs hfu hsu
  have hy : (vertShader (vp3 t a zn zf f s u eye) P).y = (1 / t) * Y := by
    show ((vp3 t a zn zf f s u eye).mulv ⟨P.x, P.y, P.z, 1⟩).y
        = (1 / t) * Y
    simp only [vp3]
    rw [mulv_mul, hlook]
    exact persp_y t a zn zf X Y D
  have hw : (vertShader (vp3 t a zn zf f s u eye) P).w = D := by
    show ((vp3 t a zn zf f s u eye).mulv ⟨P.x, P.y, P.z, 1⟩).w = D
    simp only [vp3]
    rw [mulv_mul, hlook]
    exact persp_w t a zn zf X Y D
  show pxOfNdcY
      ((vertShader (vp3 t a zn zf f s u eye) P).y
        / (vertShader (vp3 t a zn zf f s u eye) P).w) vh
    = pxOfNdcY (Y / (t * D)) vh
  rw [hy, hw, div_div Y t D ht hD]

/-- END-TO-END PICKING CORRECTNESS: take the pixel the GPU actually
    draws P at (vertShader + viewport divide), run it back through
    camera_screen_ray's analytic model, and the ray passes EXACTLY
    through P (at parameter k = D). Hover and clicks cannot disagree
    with what is on screen. -/
theorem picking_from_shader (f s u eye P : V3) (X Y D t a vw vh zn zf : Rat)
    (hV : P.sub eye = V3.add (V3.add (V3.scale D f) (V3.scale X s))
                             (V3.scale Y u))
    (hf : f.dot f = 1) (hs : s.dot s = 1) (hu : u.dot u = 1)
    (hfs : f.dot s = 0) (hfu : f.dot u = 0) (hsu : s.dot u = 0)
    (ht : t ≠ 0) (ha : a ≠ 0) (hD : D ≠ 0) (hvw : vw ≠ 0) (hvh : vh ≠ 0) :
    eye + V3.scale D
        (Ame.Camera.rayRaw f s u
          (ndcOfPxX (gpuViewport vw vh
            (vertShader (vp3 t a zn zf f s u eye) P)).1 vw)
          (ndcOfPxY (gpuViewport vw vh
            (vertShader (vp3 t a zn zf f s u eye) P)).2 vh)
          t a) = P := by
  have hnx : ndcOfPxX (gpuViewport vw vh
      (vertShader (vp3 t a zn zf f s u eye) P)).1 vw = X / (t * a * D) := by
    rw [shader_pixel_x f s u eye P X Y D t a vw vh zn zf hV hf hs hu hfs hfu
        hsu ht ha hD]
    exact ndcOfPxX_pxOfNdcX _ hvw
  have hny : ndcOfPxY (gpuViewport vw vh
      (vertShader (vp3 t a zn zf f s u eye) P)).2 vh = Y / (t * D) := by
    rw [shader_pixel_y f s u eye P X Y D t a vw vh zn zf hV hf hs hu hfs hfu
        hsu ht ha hD]
    exact ndcOfPxY_pxOfNdcY _ hvh
  exact Ame.Camera.ray_hits hV hnx hny ht ha hD


/-! ## Stage 2: the Lambert term of the one shader -/

/-- The shader's `max(_, 0.0)`. -/
def max0 (x : Rat) : Rat := if x ≤ 0 then 0 else x

theorem max0_eq_zero {x : Rat} (h : x ≤ 0) : max0 x = 0 := by
  rw [max0, if_pos h]

/-- The directional term of the engine's forward light:
    `amb + col * max0(dot(n, -dir))` (u_lamb + u_lcol * max(dot(n,-u_ldir),0))
    with scalars per channel; stated for one channel. -/
def shadeTerm (n dir : V3) (amb col : Rat) : Rat :=
  amb + col * max0 (-(n.dot dir))

/-- A surface turned AWAY from the light is lit by the ambient term
    only (the clamp kills the negative term; no subtraction cheat). -/
theorem shade_backface {n dir : V3} {amb col : Rat}
    (h : n.dot dir ≥ 0) :
    shadeTerm n dir amb col = amb := by
  have h0 : (0 : Rat) ≤ n.dot dir := h
  have hle : -(n.dot dir) ≤ 0 := by
    have hn := Rat.neg_le_neg h0
    rwa [Rat.neg_zero] at hn
  rw [shadeTerm, max0_eq_zero hle, Rat.mul_zero, Rat.add_zero]

end Ame.Shader
