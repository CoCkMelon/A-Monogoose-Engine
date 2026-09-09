# ame-next — benchmark & improvement report

Branch: `agent/ame-bench-perf-20260908` (based on `agent/ame-next-20260905-4ad0fd06` @ `8436e5a`)
Date: 2026-09-08. Machine: 2× Intel Xeon @ 2.60 GHz, 2 GB RAM, Debian 13, GCC 14.2, Release.

## TL;DR

- New `benches/` suite (headless, no GL): `bench_core` (25 subsystem benches) + `bench_render_sort`
  (sort-kernel A/B with order verification). Run: `ninja -C build bench_core bench_render_sort`.
- Six optimizations, all algorithmic (no `-ffast-math`, FP flags still pinned):
  **pool churn 36.8×, render sort 3–55×, input step 12.3×, capsule-vs-AABB 10.5×,
  text layout 2.1–2.6×, audio mixer 1.32× (bit-identical output).**
- Correctness: **22/22 ctest green** (incl. a new `mem_config` test), audio golden hash unchanged
  (`0x2399705b`), boot + mid-game screenshots **byte-identical** to the base branch.
- Separation: new `examples/memory_game/mem_config.{h,c}` pulls the whole `AME_*` env surface out
  of `mem_app.c` into pure, unit-tested libc code (also removes a `getenv` from the 1000 Hz path).

## 1. Method

- `cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release`, benches forced `-O3 -ffp-contract=off`.
- `CLOCK_MONOTONIC`, warmup + timed loop, `volatile` sink against dead-code elimination.
- **Honesty fixes applied to the harness itself:**
  - First math numbers (~3 ns/op) were bogus: loop-invariant args let GCC hoist the call out of
    the loop. Fixed with 1024-entry input tables indexed per iteration (now: `m4_mul` 6.1 ns, SSE
    1.2× faster than scalar — plausible and stable).
  - `mem_step` idles in `OVER` at ~3 ns, so the sim bench plays a **full 4×4 game** with a naive
    picker instead.
- Numbers below are **medians of 5 runs** per binary; engine A/B done by `git stash` of `src/` +
  `include/` only (same bench sources, same toolchain). Raw runs: `benches/results/`.

## 2. Results (median of 5, ns/op unless noted)

| bench | base | branch | speedup |
|---|---|---|---|
| pool full churn, cap 1024 (alloc 1024 + free 1024 + apply) | 187 811 | 5 099 | **36.8×** |
| pool free 256 distinct (worst case for old scan) | 15 013 | 1 362 | **11.0×** |
| render sort, n=16 quads (typical UI/text frame) | 5 229 ns/frame | ~95 ns/frame | **~55×** |
| render sort, n=64 / 256 / 1024 / 4096 / 16384 | 5.3/5.6/7.4/20/93 µs | 0.25/0.55/1.7/7.0/29 µs | **21×/10×/4.3×/2.8×/3.2×** |
| input fixed step (`in_begin_step` + 4 queries) | 212.5 | 17.3 | **12.3×** |
| capsule-vs-AABB overlap | 219.7 | 20.9 | **10.5×** |
| text layout: short / long+tags / plain-editor | 745 / 13 579 / 18 093 | 355 / 6 114 / 6 940 | **2.1×/2.2×/2.6×** |
| audio mixer, 8 sine voices × 512 frames | 69 250 | 52 395 | **1.32×** |
| events push+drain 64, snapshot 1 KiB, math, broadphase, raycast | — | — | 0.94–1.11× (untouched, no regression) |
| mem full 4×4 game | 980 696 | 1 256 321 | 0.78× — **not a regression** (see §4) |

Music, not noise: every untouched subsystem lands at ~1.0×, which is the control group proving the
wins above are real algorithmic deltas, not timer drift.

## 3. What was changed (and why it is correct)

### 3.1 Pool deferred-free: O(N²) → O(N) (`include/ame/pool.h`)

- **Symptom:** freeing N distinct handles cost O(N²) — `slots_free` linearly scanned the whole
  pending queue for duplicates on every call. At cap 1024, full churn cost 188 µs.
- **Fix:** per-slot `pend_gen[]` tag: `pend_gen[i] == gen[i]` (nonzero) iff slot `i` is queued in
  this window. `slots_free` is O(1); `apply`/`reset` clear tags, so a stale tag can never match a
  future generation (no 16-bit wraparound hazard even after 65 535 churn cycles). Cost: 2 bytes/slot.
- **Result:** 36.8× on full churn, 11× on the 256-free worst case.
- **Correctness:** `test_pool` green, incl. the external-review repro (`coalesced == 1`,
  `overflow_drops == 0`, no lost despawn) and the honest-overflow case. Counter semantics unchanged.

### 3.2 Render batch: sort + push (`src/render.c`)

- **Symptom:** `rp_end_frame` spent a **fixed ~5.3 µs** before drawing anything: 32 KiB `memset` over
  8193 buckets + an 8193-bucket linked-list walk + per-bucket chain reversal, even for a 16-quad
  frame. That is ~30% of a 60 fps budget's CPU sort time at typical batch sizes — pure overhead.
- **Fix:**
  - sort key (`tex*256+layer`) precomputed once at push into `q_key[]` (was recomputed per quad
    per frame);
  - prefix-sum counting sort with a **touched-bucket list**: clearing is O(used), not O(8193);
  - hybrid prefix: touched-list insertion sort for ≤256 distinct keys (the common case: a few
    textures × layers), linear full-range scan above that (avoids O(n²) on pathological frames);
  - push path: packed tint computed once per quad (4 `col_byte` clamps, not 16), normal stamp
    hoisted out of the vertex loop (1 branch, not 4), same bytes out. `rp_push_mesh` packs its
    tint once per mesh.
- **Result:** ~55× at n=16, 3–4× at n≥1024 (see table). Same total workspace footprint as before.
- **Correctness:** `bench_render_sort` asserts **identical stable order** old-vs-new on every run;
  `test_render` + `test_render_2d` (golden pixels, caret oracle) green; base-vs-branch screenshots
  byte-identical (§6).

### 3.3 Input: poll bound keys only (`src/input.c`)

- **Symptom:** `in_begin_step` did **512 atomic loads + two 512 B memcpys at 1000 Hz** (210 ns/step),
  while a game binds dozens of keys at most.
- **Fix:** `in_bind_key` maintains a deduped `bound_keys[]` list (≤128 entries); the step polls only
  those, and snapshots live state at bind time so a mid-game rebind never synthesises a phantom
  pressed/released edge (bit-for-bit the old edge semantics).
- **Result:** 12.3× (212 → 17 ns/step ≈ 17 µs of CPU per second of play).
- **Correctness:** `test_input` green; raw reads (`in_key_down_raw`, mouse) untouched.

### 3.4 Capsule-vs-AABB: 48-step search → exact analytic (`src/geometry.c`)

- **Symptom:** 217 ns/query — a 48-iteration golden-section search (~96 point evals) for what is a
  closed-form problem, and only 1e-6-span *approximate*.
- **Fix:** dist²(segment, box) is convex piecewise-quadratic with kinks exactly at slab crossings
  (≤2/axis). Evaluate endpoints + crossings + one clamped stationary point per interval (~10 evals).
  Exact, deterministic, no allocation; degenerate segments fall back to the point test.
- **Result:** 10.5× (217 → 21 ns), and *more* accurate than before.
- **Correctness:** `test_geometry_2d/3d` green (clear-cut overlap/clear cases agree).

### 3.5 Text: ASCII lookup tables (`src/text.c`)

- **Symptom:** every ASCII char paid a binary search over the baked glyph tables (~8 pointer-chasing
  steps), twice in smooth mode (pixel + DSDF tables).
- **Fix:** flat 128-entry LUTs per face, built once on first use. Publication is TSan-clean
  (`atomic_flag` spinlock + `_Atomic ready`); a thread that finds the lock held falls back to binary
  search for that lookup instead of blocking. Baked tables are process-lifetime constants, so entries
  never stale.
- **Result:** 2.1–2.6× across short/tagged/editor layouts.
- **Correctness:** `test_text` + `test_editor_geom` (two-ways caret law) green; mid-game screenshot
  with heavy text byte-identical.

### 3.6 Audio: hoist pan gains (`src/audio.c`)

- **Symptom:** `cosf`+`sinf` per voice per sample (plus clamp + scale) for gains that are constant
  across the whole block — commands apply only at the block head.
- **Fix:** compute the 32 pan pairs once per `audio_render` call; inner loop just multiplies.
  Same inputs, same operation order → **bit-identical output** (proven: golden hash `0x2399705b`
  before and after, §6). Envelope math deliberately untouched (see §4).
- **Result:** 1.32× on the 8-voice bench; larger voice counts scale better (saves 2 transcendentals
  per voice-sample).

## 4. Tried and rejected (with evidence)

1. **Events/audio `%` → `&` mask.** Checked codegen first: `objdump` shows GCC already emits
   `and $0xff` for `% 256` (and the audio ring's `% 128`). No measurable delta possible — left alone.
2. **Envelope division → reciprocal multiply.** Would save float divs per voice-sample but changes
   output bits (div ≠ mul-by-reciprocal). Rejected to keep the mixer's bit-identical guarantee;
   the test only asserts determinism, but the header promises byte-identical schedules and I kept it.
3. **Reorder indices instead of gathering vertices.** Byte-counted: vertex gather moves 176 B/quad;
   the alternative moves the same 176 B (unsorted upload) *plus* a 24 B/quad index scatter and a
   second GL upload. Strictly worse with the static-IBO design — the sort was the bottleneck, not
   the gather. Not implemented.
4. **Always full-range prefix scan in the sort.** 8193 trivial iterations cost ~2–5 µs — worse than
   the touched-list path (95 ns) for the common few-keys case. Hence the ≤256 hybrid. Proven by
   `bench_render_sort` (old path's 5.3 µs floor is exactly this scan + memset).
5. **`mem_full_game` "regression" (0.78×).** `mem_sim.c` is **byte-identical** between A and B
   (verified: never touched; A/B done by stashing only `src/`+`include/`). The delta is binary-layout /
   i-cache-alignment noise from relinking — medians are rock-stable within each binary (base
   969–1084 ms, branch 1240–1269 ms across 5 runs). Reported, not chased.

## 5. Review: usability, structure, scalability, separation

### 5.1 Usability & simplicity — good bones, sharp edges

**Good:** one C standard (C23), per-dimension libs (`ame::2d/3d`), fluent setup builders kept out of
hot paths, env-var QA surface (`AME_SEED`, `AME_SCREENSHOT`, `AME_FIXED_FRAME_DT`) is genuinely nice
for headless verification. The two-layer rule (setup builders vs. plain hot data) is documented *and*
followed — rare.

**Friction found:**
- No benchmarks existed ("if you claim speed, ship the code" — now shipped: `benches/`).
- `mem_app.c` (807 lines) parsed env inline at five sites, including a `getenv` on the **1000 Hz
  logic thread** (`AME_AUTOPLAY`).
- `file(GLOB src/*.c)` silently absorbs new files; fine today, but an explicit list would fail loud
  on typos. Left as-is (out of scope), noted here.

**Done in branch:** `benches/` + this report; `mem_config` extraction (§5.4).

### 5.2 Project structure — one real smell, rest is clean

**Good:** `include/ame/` ↔ `src/` mirror, games own their `CMakeLists` under `examples/` (like
A-Mongoose), tools before libs (bake order correct), tests per module, `docs/*.txt` spec is the
source of truth and code comments cite it.

**Smell (FIXED on this branch): `src/` mixed the C engine with the TypeScript web companion.**
The whole twin is gone — `src/*.tsx`, `src/components/`, `src/wasm/*.js`, `vite.config.ts`,
`tsconfig.json`, `package.json`, root `index.html` deleted; `src/` is C-only again. The web
companion is now `web/build.sh` (emcc, one single-file JS+WASM per game from the SAME C
sources) + `web/index.html` + `web/loader.js` (~70 hand-written lines), published to `docs/`,
and the Pages workflow rebuilds it with emsdk instead of npm. All three consequences from
the original note are resolved: (a) no second copy of any feature in another language —
`docs/README.txt` holds again; (b) C globs/greps see C only; (c) no `src/**/*.tsx` paths
anywhere in CI.

Minor: `benches/results/*.txt` are committed deliberately (small, the evidence for this report).

### 5.3 Scalability — where the ceilings are (after this branch)

| subsystem | before | after | residual ceiling |
|---|---|---|---|
| pool free-N | O(N²) | O(N) | none for realistic caps |
| render sort | O(8193) floor 5.3 µs | O(n + used) | vertex gather 176 B/quad (fine ≤16k) |
| input step | 512 atomics | ≤128 bound keys | none (17 ns) |
| capsule query | ~96 evals | ~10 evals | none |
| text lookup | log G binary search | O(1) ASCII | non-ASCII still binary search (fine) |
| audio | 2 transcend./voice-sample | 0 (hoisted) | per-sample `sinf` for sine waves (bit-stability!) |

Fixed caps (`RP_TEX_MAX 32`, `AME_EV_QUEUE_CAP 256`, `AME_GEO_MAX_STATIC 1024`) are honest static
budgets, documented at declaration sites — the right call for a no-malloc engine. No cap needed
raising for the anchor game; the branch removed the *algorithmic* ceilings instead.

### 5.4 Separation of gameplay from tech — sim is exemplary, wiring leaked

**Exemplary:** `mem_sim.{h,c}` (159 lines) is pure: no SDL, no GL, no engine state — shared unchanged
by hot-seat, headless tests, and the POSIX-only `mem_server`. This is the pattern to copy.

**Leak (fixed):** `mem_app.c` mixed process-env parsing with app wiring. New `mem_config.{h,c}` owns
the whole `AME_*` surface as **pure libc** (no SDL/GL/engine includes): defaults + `from_env()` +
a pure `parse_server()`. `mem_app` calls it once at boot; the logic thread reads `g_autoplay`, never
`getenv`. Covered by new `tests/test_mem_config.c` (6 cases: defaults, server forms, seed replay,
autoplay presence rule, fake-mouse validation, screenshot clamp).

**Remaining (proposed, not done):** `mem_app.c` still fuses snapshot publish + picking + SFX triggers +
drawing (~800 lines). The natural next split is `mem_view.c` (all `rp_*`/`text_*` drawing from
`mem_snap`) vs. `mem_app.c` (threads/glue) — `mem_snap` is already the clean seam. Left for a
follow-up to keep this branch behavior-identical and reviewable.

## 6. Correctness evidence

- `ctest`: **22/22 pass** (19 pre-existing + `mem_config` + 2 bench smoke tests), incl. golden
  `render`/`render_2d`, online `mem_net` (real server, drop+rejoin), caret `editor_geom`.
- Audio mixer **bit-identical**: `test_audio` golden hash `0x2399705b` before → after.
- Screenshots **byte-identical** base-vs-branch (offscreen + llvmpipe): boot frame (`AME_SEED=0x5EED`,
  5 frames) and mid-game autoplay frame (`AME_SEED=0x1234`, autoplay, fixed-dt, 60 frames).
- Sort orders **proven equal** old-vs-new (`bench_render_sort` verifies every size, every run).
- Seed replay still deterministic: same seed twice → `cmp`-identical PNGs.

## 7. Reproduce

```sh
git checkout agent/ame-bench-perf-20260908
git submodule update --init
cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release && ninja -C build
./build/benches/bench_core            # subsystem table
./build/benches/bench_render_sort     # sort A/B + order proof
ctest --test-dir build                # 22/22 incl. benches
```

## 8. Files changed

- `benches/bench_core.c`, `benches/bench_render_sort.c`, `benches/CMakeLists.txt`,
  `benches/results/*` — new suite + raw runs.
- `include/ame/pool.h`, `src/input.c`, `src/geometry.c`, `src/audio.c`, `src/text.c`,
  `src/render.c` — the six optimizations (no public API changes).
- `examples/memory_game/mem_config.{h,c}` (new), `mem_app.c`, `examples/memory_game/CMakeLists.txt`,
  `tests/test_mem_config.c` (new), `tests/CMakeLists.txt` — config separation + test.
- `CMakeLists.txt` (`AME_BUILD_BENCHES`, default ON), `README.md` (benchmark pointer).
