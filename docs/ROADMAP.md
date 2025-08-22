# A Mongoose Engine – Roadmap

Purpose
- Capture a pragmatic long-term plan for evolving this C engine into a lean C++ game framework.
- Define a short-term exploration track that avoids scene files and physics to iterate on gameplay quickly.

Guiding principles
- Start simple, ship a playable loop early.
- Prefer data-minimal workflows at first (no heavy editors/pipelines).
- Keep rendering single-pass with one material and a pixel-perfect camera.
- Introduce ECS and asset pipelines only when they remove friction.
- Avoid over-dependency: keep each lib choice justifiable by clear benefit.

Phases overview
- Phase 0: Foundations and constraints
- Phase 1: No-scene, no-physics gameplay exploration (current focus)
- Phase 2: ECS integration + sprite batch renderer
- Phase 3: Tilemap import (Tiled) and multilayer rendering
- Phase 4: Physics integration (Jolt or Box2D) – optional, when needed
- Phase 5: Scene assembly (USD or a simple scene pack) – optional, when needed

---

Phase 0 – Foundations and constraints
- Language/build: move toward C++20 with CMake while keeping current C app working.
- Platform: SDL3 for windowing, timing; custom high-performance input (asyncinput).
- Renderer: OpenGL 4.5 core; single material, single pass; batching for sprites/lines.
- Camera: pixel-perfect orthographic camera with integer snapping and integer zoom.
- Asset loading: basic images (stb_image), simple text/binary blobs.
- Deliverable: current curve painting sample kept as a diagnostic mode.

Success criteria
- Stable 60/120 Hz update/render with negligible input latency.
- All visual elements align perfectly to pixel grid with nearest sampling.
- Positions are correct.

---

Phase 1 – No-scene, no-physics gameplay exploration (current focus)
Goal: Quickly prototype gameplay loops without any scene files or physics engine.

Scope
- Hardcoded level data: arrays/const tables in code; optional tiny JSON for parameters, not “scene graphs”.
- Entities as POD structs or lightweight arrays; no ECS requirement yet.
- Collision: none or extremely simple AABB checks written in-house.
- Rendering: single-pass sprite/line batch, texture atlas optional but encouraged.
- Camera: pixel-perfect with integer position and zoom; culling optional.
- Input: asyncinput for mouse; SDL for keyboard/gamepad.
- Audio: optional later.

Short-term tasks
- Implement a sprite batcher:
  - Dynamic vertex buffer; position/uv/color/layer packed per-vertex.
  - One atlas page initially; nearest-neighbor sampling, clamp-to-edge; 2–4px padding around sprites to avoid bleeding.
- Implement a pixel-perfect camera helper:
  - Orthographic projection sized to the window; integer snap camera position.
  - Only allow integer zoom steps.
- Build a tiny game loop sample:
  - Player sprite moving on a grid; keyboard controls; screen wrap or simple bounds.
  - A few dynamic objects (collectibles) updated with simple timers/state machines.
  - Optional AABB overlap for pickup; no physics integration.
- Add a frame timing overlay and hot-reload for a single texture (optional).

Milestones
- M1: Sprite batch draws N sprites at 60+ FPS; camera snap verified.
- M2: Minimal game loop (player movement, collectibles, score, restart).
- M3: Hardcoded multilayer background with parallax via integer offsets.

Exit criteria
- At least one microgame is playable and demonstrates render/camera/input reliability.

---

Phase 2 – ECS integration + renderer consolidation
Goal: Introduce flecs for scalable entity management without changing gameplay feel.

Scope
- Add flecs, define minimal components: Transform2D, Sprite, ScriptTag, Timer.
- Systems: RenderSystem, Script/UpdateSystem, CameraSystem.
- Keep no-scene approach: entities created procedurally at startup.

Deliverables
- Sample: previous microgame rebuilt with ECS; performance parity or better.

---

Phase 3 – Tilemap import (Tiled) and multilayer rendering
Goal: Author levels in Tiled and render multilayer maps efficiently.

Scope
- TMX/TSX importer (tinyxml2/pugixml):
  - Bake to a compact .tilebin (or JSON) with tile indices and UVs.
  - Optional chunking (e.g., 32x32 tiles per chunk) and static VBOs per layer.
- ECS components: TileLayer, Tilemap.
- Rendering: batch tiles with sprites in one pass when sharing atlas page.
- Collision: still optional; may derive simple blocker grid from Tiled object layer.

Deliverables
- Sample level authored in Tiled with 2–3 layers and object markers (spawn points).

---

Phase 4 – Physics integration (optional; when needed)
Options
- Jolt Physics (future 3D, robust stacking) using 2D constraints/flattened Z.
- Box2D (lean 2D) for arcade/platformers.

Scope
- PhysicsSystem to step world; sync Body <-> Transform.
- Basic shapes: boxes/circles; simple filters; kinematic bodies for movers.

Exit criteria
- Stable sim at target FPS and predictable gameplay feel.

---

Phase 5 – Scene assembly (optional; when needed)
Options
- USD-based authoring for complex scenes and composition; bake to runtime packs.
- Or a simple custom scene JSON that references tilebins/atlases/prefabs.

Scope
- Importers in tools/ to convert authoring formats into engine-native binaries.
- Runtime loader that spawns ECS entities from packs.

---

Technical notes
Pixel-perfect camera
- Use ortho projection matching window size; ensure vertex positions land on integer pixel centers.
- Snap camera position to integer world pixels before building the view matrix.
- Restrict zoom to integer factors; if scaling the viewport, prefer integer scaling to avoid resampling.
- Use GL_NEAREST sampling; add atlas padding to prevent bleeding.

Renderer batching
- Maintain a persistent mapped VBO/IBO with orphaning or persistent mapping.
- Sort by atlas page, then by layer; avoid state changes.
- Keep per-sprite color tint and layer; consider texture arrays if multi-page is needed later.

Dependencies
- Mandatory (near term): SDL3, OpenGL 4.5, stb_image, tinyxml2 (when TMX arrives), flecs (Phase 2).
- Optional (later): Jolt or Box2D, OpenUSD, Dear ImGui for debug UI.

Tooling
- tools/ (future):
  - tmx_import: TMX -> .tilebin
  - pack: bundle textures/levels
  - usd_import: USD -> scene packs (later)

Testing & CI
- Add a headless unit for math/camera snapping.
- Capture golden images for sprite batch regression (optional).

Risks & mitigations
- Overengineering early: keep Phase 1 lightweight; defer ECS/physics.
- Dependency sprawl: gate heavy libs behind phases; vendor only when necessary.
- Pixel artifacts: enforce integer snap and atlas padding; test at various window sizes.

Adoption path
- Stay in Phase 1 until a satisfying microgame exists.
- Move to Phase 2 to reduce gameplay code churn and enable larger scenes.
- Only add tilemaps/physics/scene systems when the gameplay demands them.

---

Guidelines for AI agents
- Keep logging DEBUG-only unless errors must be surfaced. Prefer a guarded macro pattern in C, for example:

      #ifdef DEBUG
      #define LOGD(...) SDL_Log(__VA_ARGS__)
      #else
      #define LOGD(...) ((void)0)
      #endif

- Retain essential diagnostics under LOGD.
- Use plain ASCII in C source. Avoid special Unicode, HTML, or nonstandard escapes in code.
- Make minimal, scoped changes and preserve current behavior in Release builds.
- Use clear commit messages and add your model name if you are AI.

