# ame-next — Lean 4 model

Pure Lean 4 (v4.33.1, **no mathlib, no `sorry`**) formal model of the
engine's **geometry and coordinate conventions** — the layer where the
picking bug lived — plus its event/pool/loop contracts and the Memory
rules. Build:

```
elan default stable   # once (leanprover/lean4:v4.33.1)
lake build            # in lean/
```

## Geometry & coordinates (primary use case)

The engine's exact formulas, mirrored line-for-line and proved in
general (all parameters abstract, scalars = exact `Rat`; `tan(fov/2)`
is a rational parameter, normalization is stated as ray *parallelism*
so no √ is needed):

| Module | Mirrors (C) | What is proved |
|---|---|---|
| `Ame/Geo.lean` | `ame_v3_*`, px/NDC conventions of `camera.c` | pixel↔NDC round-trips **in both axes and both directions** (the y-flip convention can never eat a coordinate); dot-linearity; reading `X Y D` camera coordinates off an orthonormal basis |
| `Ame/M4.lean` | `ame_m4_*` in `math.h` | `M * I = I * M = M`; structured inverses proven by direct multiplication: `translate(t)·translate(−t) = I`, `scale·reciprocal = I`, `rotY(c,s)·rotY(c,−s) = I` (given `c²+s²=1`); 2D corner convention (world (0,0) → NDC (−1,+1)); the **adjugate inverse transcribed exactly** from `ame_m4_inverse`, with concrete `M·M⁻¹ = I` spot-checks mirroring `tests/test_camera.c` |
| `Ame/Camera.lean` | `camera_screen_ray` (analytic) | **Picking soundness**: if `P − pos = D·f + X·s + Y·u` and (sx,sy) is the pixel the camera projects P to, then `pos + D·rayRaw(sx,sy) = P` — the pick ray passes exactly through P, hit parameter k = D. Fully general: any orthonormal basis, any viewport, any t/aspect/depth |
| `Ame/Shader.lean` | `gl_Position = u_vp * vec4(a_pos,1.0)` + GPU divide/viewport | the look-at stage sends P to `(X, Y, −D, 1)`; the perspective stage scales x by `1/(t·a)` and makes w = D; **shader pixel == analytic camera pixel** (x and y); **end-to-end**: the pixel the GPU actually draws P at, fed back through `camera_screen_ray`'s model, hits P exactly — hover/clicks cannot disagree with what is on screen |

### Axiom status

`#print axioms` on every theorem: standard Lean axioms only
(`propext`, `Quot.sound`, `Classical.choice`). The ONE exception,
clearly quarantined in `Ame/M4.lean`: the four concrete inverse
spot-checks use `native_decide` (compiled evaluation of exact
rationals — the kernel cannot reduce `Rat` division) and carry
`Lean.ofReduceBool`/`Lean.trustCompiler`. Every other theorem is
kernel-checked.

### Scope notes (deliberate)

- `lookAt` takes the (already orthonormal) basis as arguments; the C
  normalizes internally (∉ ℚ).
- The pick ray is the RAW (unnormalized) direction — normalization
  does not change which points a ray passes through.
- 2D ortho screen↔world round-trip under zoom is covered by C tests
  (`test_camera.c`) and the corner convention here; a Lean version is
  future work (needs `add_div`-style lemmas core lacks).

## Engine contracts & game rules (secondary model)

| Module | Models (C source) | Theorems |
|---|---|---|
| `Ame.Basic` | fixed-step loop determinism (`src/loop.c`) | `run_deterministic` (agreeing steps ⇒ identical runs — replay), `run_append` (checkpoint composition) |
| `Ame.Pool` | slot pools (`include/ame/pool.h`) | `alloc_valid`, `alloc_none_when_full`, `valid_untouched_by_free`, `applyOne_invalidates`/`applyOne_gen` (generation handles) |
| `Ame.EventRing` | bounded event queue (`include/ame/events.h`) | `push_bounded`, `push_no_drop`/`push_overflow_untouched`, `push_overflow_counts_drop` (drop-oldest is never silent) |
| `Ame.MemGame` | Memory rules (`examples/memory_game/`) | `turn_flips` (turn passes every resolve), `no_match_keeps`, `match_scores_once`, `mark_count_step` |
