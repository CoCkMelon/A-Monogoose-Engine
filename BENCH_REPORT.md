# ame-next bench report — struct-args batch + pool free-list

Branch: `agent/ame-next-struct-batch-20260909`
Host: Linux x86_64, GCC 14, `-O3` / Release, 2026-09-09.

## What changed

1. **HOT renderer: one struct pointer per call.**
   Every `ame_batch_*`, `ame_font_draw`, `ame_debug_submit`, and
   `ame_vertex_make` takes a single `const ame_*_args *`. The pipeline
   lives inside the args. Call sites use C99 compound literals. SETUP
   builders (`ame_pipeline_*`, `ame_camera_*`) stay chainable multi-step
   pointers — the rule is for the HOT path only.

2. **Batch internals.**
   - `ame_batch_box` transforms the 8 corners once (was 24 `m4_mul_point`).
   - Quad/vertex packing writes verts directly (no per-vert `make()` hop
     on the inner path).

3. **Pool free-list + O(1) despawn dedupe** (`ame_pool_bind_fast`).
   Optional `free_list` + `pend_gen` arrays. Without them behaviour is
   unchanged (linear scan). Memory cards and Biscuit fuel/mine pools opt
   in. Spawn still returns the lowest free index first.

4. **Separation.**
   Gameplay still owns `BfSnap` / `MemSnap`; render only reads. Sim TUs
   never include GL. Documented in README.

5. **`benches/bench_core`** headless suite (CMake option `AME_BUILD_BENCHES`).

## Correctness

| Check                         | Result        |
|-------------------------------|---------------|
| `ctest` (20 suites)           | 20/20 pass    |
| `memory --selftest` BMP       | byte-identical to pre-change |
| `biscuit --selftest` BMP      | byte-identical to pre-change |
| pool fast-path unit coverage  | in `test_pool` |
| batch box/rect vert counts    | in `test_batch` |

## Numbers (`./build/bench_core`)

```
case                               ns/op        Mops/s  note
pool_linear_fill_drain          961.83 ns/op        1.04 Mops/s  spawn+despawn (no free_list)
pool_fast_fill_drain              4.95 ns/op      202.16 Mops/s  spawn+despawn (free_list+pend_gen)
pool_valid                        2.34 ns/op      426.91 Mops/s  handle validation
batch_xy_rect                    26.14 ns/op       38.25 Mops/s  struct-args push (6 verts each)
batch_box                       157.57 ns/op        6.35 Mops/s  8-corner transform + 36 verts
batch_cylinder_z                880.10 ns/op        1.14 Mops/s  14-seg wheel
geo_circle_seg_xy                 6.05 ns/op      165.20 Mops/s  wheel vs track
geo_point_in_aabb_xy              3.09 ns/op      323.17 Mops/s  Memory card pick
snap_publish                     23.53 ns/op       42.49 Mops/s  seqlock write
snap_latest_copy                 32.60 ns/op       30.68 Mops/s  seqlock read copy-out
memory_tick_1ms                  13.68 ns/op       73.12 Mops/s  60k fixed steps
m4_mul                           16.60 ns/op       60.24 Mops/s  column-major 4x4 (chained)
font_draw_hud                    25.45 ns/op       39.29 Mops/s  glyph quads / char
```

**Pool free-list vs linear scan: ~194×** on fill/drain of 4096 slots
(962 ns/op → 4.95 ns/op). Small game pools (16 cards, 12 fuel) still
benefit on respawn churn; the API stays simple when `bind_fast` is skipped.

## API sketch

```c
ame_batch_begin(&(ame_batch_begin_args){ .p = pipe });
ame_batch_xy_rect(&(ame_batch_xy_rect_args){
    .p = pipe, .x = 0, .y = 0, .z = 0, .w = 1, .h = 1,
    .uv = white, .color = one
});
ame_batch_box(&(ame_batch_box_args){
    .p = pipe, .world = M, .half_extents = v3(hx, hy, hz),
    .uv_pos_z = face, .uv_neg_z = back, .color = one
});
ame_font_draw(&(ame_font_draw_args){
    .p = pipe, .font = &font, .x = x, .y = y, .z = z,
    .pixel_size = 0.05f, .text = "P1", .color = red
});
ame_batch_flush(&(ame_batch_flush_args){
    .p = pipe, .view_projection_4x4 = ame_camera_vp(cam)
});
```

## Layout reminder

```
include/ame/     engine public API (no game types)
src/             matching .c
examples/memory  gameplay snapshot + render/ only draws MemSnap
examples/biscuit gameplay/physics/entities + render/ only draws BfSnap
benches/         headless micro-benches
tests/           ctest suite
```

---

## Round 2 — static level mesh, audio unlock, phys broadphase, ame_app host

Branch: `agent/ame-next-improve-2-20260909`

### Changes
1. **Static level mesh** — biscuit ribbon (2392 tris / 4784 verts) is converted
   once at view init, uploaded as `ame_mesh`, drawn with
   `ame_pipeline_draw_mesh` (one DrawElements). Dynamic batch no longer
   re-pushes ~7k verts every frame for the track.
2. **Audio mix** — snapshot voices under mutex, synthesise unlocked, write
   back only slots whose generation is unchanged (play_tone bumps gen).
   Pan gains hoisted per block. Mix lock no longer spans the whole block.
3. **Physics segment broadphase** — segs sorted by min-x once via
   `phys_world_prepare`; circle queries binary-search + walk the x-window.
4. **Games use `ame_app`** — Memory and Biscuit boot/swap/close through the
   shared host (window/GL/audio). Duplicated SDL open paths removed.

### Correctness
- 20/20 ctest
- selftest BMP hashes unchanged vs round 1 (`c796eb…` / `0ac19b…`)

### Bench add
```
audio_mix_block                   ~8.8 ns/op   stereo frames (unlocked synth)
```
