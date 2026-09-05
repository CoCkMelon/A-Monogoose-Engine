# Lean 4 model of ame-next

A small, mathlib-free model of the C engine sitting in `../`.
It is a *semantic* model: types and theorems, not a bit-accurate
port of `uint32_t` wrap or GL.

| Lean | C |
|---|---|
| `Ame.Handle` | `ame_handle` (index + generation; gen 0 invalid) |
| `Ame.Pool` | `ame_pool` (spawn, deferred despawn, apply) |
| `Ame.Event` | `ame_events_*` (bounded FIFO, drop-oldest) |
| `Ame.Geo` | `ame_geo_*` (AABB XY pick; circle vs segment; ray-as-spec) |
| `Ame.Math` | 90° yaw stand-in for `quat_from_axis_angle` Z |
| `Ame.Memory` | `mem_*` (hotseat Memory rules) |
| `Ame.Loop` | callback = click; main = resolve + snapshot |

Build:

```
export PATH="$HOME/.elan/bin:$PATH"
cd lean && lake build
```

No 1000 Hz thread is modelled — this game resolves on a hold, then
renders. SETUP vs HOT is recorded in `Ame.Loop`.

## C generation

Error-prone kernels are written as an Imp AST in `Ame/Kernels.lean`,
proved against `Handle` / `Geo` on the examples, and pretty-printed
to `../generated/ame_gen.{h,c}`:

```
lake exe ame-gen ../generated
```

Hand-written `src/` stays the game binary. `test_gen` compiles the
emitted C and checks handle packing, AABB pick, spawn, match, mismatch.
