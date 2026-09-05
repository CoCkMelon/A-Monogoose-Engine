/-
Copyright (c) 2025 ame-next contributors.
Formal model of the ame-next mat4 layer (include/ame/math.h):
column-major, right-handed, clip z in [-1,1] (GL convention).
Pure Lean 4, no mathlib. Scalars are Rat; tan(fov/2) is a parameter `t`.
-/
import Ame.Geo

namespace Ame

/-! ## V4 arithmetic -/

def V4.zero : V4 := ⟨0, 0, 0, 0⟩

def V4.add (a b : V4) : V4 := ⟨a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w⟩
def V4.scale (k : Rat) (v : V4) : V4 := ⟨k * v.x, k * v.y, k * v.z, k * v.w⟩

instance : Add V4 := ⟨V4.add⟩

/-! ## Mat4 (ame_m4: m[c*4 + row], modeled as four columns) -/

structure Mat4 where
  c0 : V4
  c1 : V4
  c2 : V4
  c3 : V4
  deriving DecidableEq

/-- ame_m4_mul expressed per column: `r[col] = M * b[col]`. -/
def Mat4.mulv (m : Mat4) (v : V4) : V4 :=
  V4.add (V4.scale v.x m.c0)
    (V4.add (V4.scale v.y m.c1)
      (V4.add (V4.scale v.z m.c2) (V4.scale v.w m.c3)))

/-- ame_m4_mul (apply b first, then a). -/
def Mat4.mul (a b : Mat4) : Mat4 :=
  ⟨a.mulv b.c0, a.mulv b.c1, a.mulv b.c2, a.mulv b.c3⟩

instance : Mul Mat4 := ⟨Mat4.mul⟩

/-- ame_m4_identity -/
def Mat4.id : Mat4 := ⟨⟨1, 0, 0, 0⟩, ⟨0, 1, 0, 0⟩, ⟨0, 0, 1, 0⟩, ⟨0, 0, 0, 1⟩⟩

/-- ame_m4_translate -/
def Mat4.translate (t : V3) : Mat4 :=
  ⟨⟨1, 0, 0, 0⟩, ⟨0, 1, 0, 0⟩, ⟨0, 0, 1, 0⟩, ⟨t.x, t.y, t.z, 1⟩⟩

/-- ame_m4_scale -/
def Mat4.scaleM (s : V3) : Mat4 :=
  ⟨⟨s.x, 0, 0, 0⟩, ⟨0, s.y, 0, 0⟩, ⟨0, 0, s.z, 0⟩, ⟨0, 0, 0, 1⟩⟩

/-- ame_m4_rot_y; cos/sin enter as parameters, `c*c + s*s = 1` in theorems. -/
def Mat4.rotY (c s : Rat) : Mat4 :=
  ⟨⟨c, 0, -s, 0⟩, ⟨0, 1, 0, 0⟩, ⟨s, 0, c, 0⟩, ⟨0, 0, 0, 1⟩⟩

/-- ame_m4_perspective, with `t := tan (fov_y / 2)`, `a := aspect`. -/
def Mat4.persp (t a zn zf : Rat) : Mat4 :=
  ⟨⟨1 / (t * a), 0, 0, 0⟩,
   ⟨0, 1 / t, 0, 0⟩,
   ⟨0, 0, (zf + zn) / (zn + -zf), -1⟩,
   ⟨0, 0, (2 * zf * zn) / (zn + -zf), 0⟩⟩

/-- ame_m4_ortho_px (x right, y DOWN, 1 unit = 1 px at zoom 1). -/

def Mat4.orthoPx (w h zn zf : Rat) : Mat4 :=
  ⟨⟨2 / w, 0, 0, 0⟩,
   ⟨0, -2 / h, 0, 0⟩,
   ⟨0, 0, 1 / (zf + -zn), 0⟩,
   ⟨-1, 1, -zn / (zf + -zn), 1⟩⟩
/-- Symmetric-box orthographic projection, gl-matrix/cglm convention
    (NDC [-1,1], camera looks down -z). ame_m4_ortho in include/ame/math.h
    is the C twin; the shadow pass builds its light view-projection with
    it (Stage 2). -/
def Mat4.ortho (l r b t zn zf : Rat) : Mat4 :=
  ⟨⟨2/(r-l), 0, 0, 0⟩,
   ⟨0, 2/(t-b), 0, 0⟩,
   ⟨0, 0, -2/(zf-zn), 0⟩,
   ⟨-(r+l)/(r-l), -(t+b)/(t-b), -(zf+zn)/(zf-zn), 1⟩⟩


/-- ame_m4_look_at. The C version normalizes `f = norm(look-eye)`,
    `s = norm(cross f up)`, `u = cross s f` internally; the model takes the
    (already orthonormal) basis as arguments and carries normality in
    theorem hypotheses instead of computing square roots. -/
def Mat4.lookAt (f s u eye : V3) : Mat4 :=
  ⟨⟨s.x, u.x, -f.x, 0⟩,
   ⟨s.y, u.y, -f.y, 0⟩,
   ⟨s.z, u.z, -f.z, 0⟩,
   ⟨-(s.dot eye), -(u.dot eye), f.dot eye, 1⟩⟩

/-! ## Inverse (adjugate, mirroring ame_m4_inverse exactly) -/

/-- General 4x4 inverse, adjugate method (gl-matrix formula). Returns
    identity when the determinant is exactly zero (C: |det| < 1e-12 with
    floats; the model is exact, so the threshold collapses to det = 0). -/
def Mat4.inv (m : Mat4) : Mat4 :=
  let a00 := m.c0.x; let a01 := m.c0.y; let a02 := m.c0.z; let a03 := m.c0.w
  let a10 := m.c1.x; let a11 := m.c1.y; let a12 := m.c1.z; let a13 := m.c1.w
  let a20 := m.c2.x; let a21 := m.c2.y; let a22 := m.c2.z; let a23 := m.c2.w
  let a30 := m.c3.x; let a31 := m.c3.y; let a32 := m.c3.z; let a33 := m.c3.w
  let b0 := a00 * a11 + -(a01 * a10);  let b1 := a00 * a12 + -(a02 * a10)
  let b2 := a00 * a13 + -(a03 * a10);  let b3 := a01 * a12 + -(a02 * a11)
  let b4 := a01 * a13 + -(a03 * a11);  let b5 := a02 * a13 + -(a03 * a12)
  let b6 := a20 * a31 + -(a21 * a30);  let b7 := a20 * a32 + -(a22 * a30)
  let b8 := a20 * a33 + -(a23 * a30);  let b9 := a21 * a32 + -(a22 * a31)
  let b10 := a21 * a33 + -(a23 * a31); let b11 := a22 * a33 + -(a23 * a32)
  let det := b0 * b11 + -(b1 * b10) + b2 * b9 + b3 * b8 + -(b4 * b7) + b5 * b6
  if det = 0 then Mat4.id else
  let id := 1 / det
  ⟨⟨(a11 * b11 + -(a12 * b10) + a13 * b9) * id,
    (a02 * b10 + -(a01 * b11) + -(a03 * b9)) * id,
    (a31 * b5 + -(a32 * b4) + a33 * b3) * id,
    (a22 * b4 + -(a21 * b5) + -(a23 * b3)) * id⟩,
   ⟨(a12 * b8 + -(a10 * b11) + -(a13 * b7)) * id,
    (a00 * b11 + -(a02 * b8) + a03 * b7) * id,
    (a32 * b2 + -(a30 * b5) + -(a33 * b1)) * id,
    (a20 * b5 + -(a22 * b2) + a23 * b1) * id⟩,
   ⟨(a10 * b10 + -(a11 * b8) + a13 * b6) * id,
    (a01 * b8 + -(a00 * b10) + -(a03 * b6)) * id,
    (a30 * b4 + -(a31 * b2) + a33 * b0) * id,
    (a21 * b2 + -(a20 * b4) + -(a23 * b0)) * id⟩,
   ⟨(a11 * b7 + -(a10 * b9) + -(a12 * b6)) * id,
    (a00 * b9 + -(a01 * b7) + a02 * b6) * id,
    (a31 * b1 + -(a30 * b3) + -(a32 * b0)) * id,
    (a20 * b3 + -(a21 * b1) + a22 * b0) * id⟩⟩

/-! ## Identity multiplication -/

theorem Mat4.mul_id_left (m : Mat4) : m * Mat4.id = m := by
  show Mat4.mul m Mat4.id = m
  simp [Mat4.mul, Mat4.mulv, Mat4.id, V4.scale, V4.add, Rat.mul_one,
        Rat.mul_zero, Rat.one_mul, Rat.zero_mul, Rat.add_zero, Rat.zero_add]

theorem Mat4.mul_id_right (m : Mat4) : Mat4.id * m = m := by
  show Mat4.mul Mat4.id m = m
  simp [Mat4.mul, Mat4.mulv, Mat4.id, V4.scale, V4.add, Rat.mul_one,
        Rat.mul_zero, Rat.one_mul, Rat.zero_mul, Rat.add_zero, Rat.zero_add]

/-! ## Structured inverses (correctness by direct multiplication) -/

/-- Translation is undone by negated translation. -/
theorem Mat4.translate_mul_translate (t : V3) :
    Mat4.translate t * Mat4.translate ⟨-t.x, -t.y, -t.z⟩ = Mat4.id := by
  show Mat4.mul _ _ = _
  simp [Mat4.mul, Mat4.mulv, Mat4.translate, Mat4.id, V4.scale, V4.add,
        Rat.mul_one, Rat.mul_zero, Rat.one_mul, Rat.zero_mul, Rat.add_zero,
        Rat.zero_add, Rat.add_neg_cancel, Rat.neg_add_cancel]

/-- Scaling is undone by reciprocal scaling. -/
theorem Mat4.scaleM_mul_scaleM (s : V3) (hx : s.x ≠ 0) (hy : s.y ≠ 0)
    (hz : s.z ≠ 0) :
    Mat4.scaleM s * Mat4.scaleM ⟨1 / s.x, 1 / s.y, 1 / s.z⟩ = Mat4.id := by
  show Mat4.mul _ _ = _
  simp [Mat4.mul, Mat4.mulv, Mat4.scaleM, Mat4.id, V4.scale, V4.add,
        Rat.mul_one, Rat.mul_zero, Rat.one_mul, Rat.zero_mul, Rat.add_zero,
        Rat.zero_add, Rat.div_mul_cancel hx, Rat.div_mul_cancel hy,
        Rat.div_mul_cancel hz]

/-- A Y rotation is undone by rotating the other way (c,s from a unit
    circle: `c*c + s*s = 1`). -/
theorem Mat4.rotY_mul_rotY (c s : Rat) (h : c * c + s * s = 1) :
    Mat4.rotY c s * Mat4.rotY c (-s) = Mat4.id := by
  show Mat4.mul _ _ = _
  have h' : s * s + c * c = 1 := by rw [Rat.add_comm (s * s) (c * c), h]
  simp only [Mat4.mul, Mat4.mulv, Mat4.rotY, Mat4.id, V4.scale, V4.add,
      Rat.mul_one, Rat.mul_zero, Rat.one_mul, Rat.zero_mul, Rat.add_zero,
      Rat.zero_add, Rat.neg_neg, Rat.mul_neg, Rat.neg_mul, Rat.mul_comm,
      rat_mul_left_comm, Rat.mul_assoc, Rat.add_assoc, Rat.add_left_comm,
      Rat.add_neg_cancel, Rat.neg_add_cancel, h']
  rw [h]



/-! ## Concrete inverse spot-checks (mirror of tests/test_camera.c)

Kernel-checked simp cannot evaluate Rat division (extern ops, see
AGENT_HANDOFF), so these use `native_decide`: compiled evaluation over
exact rationals. `#print axioms` shows `Lean.ofReduceBool` +
`Lean.trustCompiler` on EXACTLY these five theorems; every other
theorem in this model is kernel-checked. They guard the TRANSCRIPTION
of ame_m4_inverse - the bug class that broke picking in the C code
(hand-written cofactors). Irrational C parameters (rotY 0.7, fov 0.9)
are replaced by nearby rational ones (3-4-5 rotation, t = 1/2). -/

theorem inv_translate : Mat4.translate ⟨3, -4, 5⟩ * (Mat4.translate ⟨3, -4, 5⟩).inv
    = Mat4.id := by native_decide

theorem inv_rotscale :
    (Mat4.rotY (3 / 5) (4 / 5) * Mat4.scaleM ⟨2, 2, 2⟩) *
      (Mat4.rotY (3 / 5) (4 / 5) * Mat4.scaleM ⟨2, 2, 2⟩).inv
    = Mat4.id := by native_decide

theorem inv_persp :
    Mat4.persp (1 / 2) (16 / 9) (1 / 10) 100 *
      (Mat4.persp (1 / 2) (16 / 9) (1 / 10) 100).inv
    = Mat4.id := by native_decide

theorem inv_lookAt :
    Mat4.lookAt ⟨0, 3, 4⟩ ⟨1, 0, 0⟩ ⟨0, 4 / 5, -3 / 5⟩ ⟨0, 3, 4⟩ *
      (Mat4.lookAt ⟨0, 3, 4⟩ ⟨1, 0, 0⟩ ⟨0, 4 / 5, -3 / 5⟩ ⟨0, 3, 4⟩).inv
    = Mat4.id := by native_decide

/-- 2D pixel space: world (0,0) is the TOP-LEFT corner, which maps to
    NDC (-1, +1) - y is DOWN in px space, UP in NDC. -/
theorem orthoPx_origin (w h zn zf : Rat) :
    ((Mat4.orthoPx w h zn zf).mulv ⟨0, 0, 0, 1⟩).x = -1 ∧
    ((Mat4.orthoPx w h zn zf).mulv ⟨0, 0, 0, 1⟩).y = 1 := by
  constructor
  · show 0 * (2 / w) + (0 * 0 + (0 * 0 + 1 * -1)) = -1
    simp [Rat.mul_zero, Rat.zero_mul, Rat.mul_one, Rat.zero_add]
  · show 0 * 0 + (0 * (-2 / h) + (0 * 0 + 1 * 1)) = 1
    simp [Rat.mul_zero, Rat.zero_mul, Rat.mul_one, Rat.zero_add]

end Ame
