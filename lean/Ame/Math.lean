/-
  Tiny integer stand-in for C `quat` / `vec3`.
  90° about Z is what Biscuit yaw uses; continuous quats stay in C.
-/
namespace Ame.Math

structure Vec3 where
  x : Int
  y : Int
  z : Int
  deriving DecidableEq, Repr

/-- `quat_from_axis_angle((0,0,1), π/2)` on XY. -/
def rotate90z (v : Vec3) : Vec3 := ⟨-v.y, v.x, v.z⟩

theorem rot90_i : rotate90z ⟨1, 0, 0⟩ = ⟨0, 1, 0⟩ := rfl
theorem rot90_j : rotate90z ⟨0, 1, 0⟩ = ⟨-1, 0, 0⟩ := rfl
theorem rot90_four (v : Vec3) :
    rotate90z (rotate90z (rotate90z (rotate90z v))) = v := by
  cases v <;> rfl

end Ame.Math
