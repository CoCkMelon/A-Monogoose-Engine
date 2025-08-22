Agent Handoff Notes

Context snapshot (2025-08-22)
- Repo: a_mongoose_engine
- Example target: examples/unitylike_minimal
- Platform: Linux (Arch), shell zsh, SDL3 + OpenGL (glad)

High-level status
- unitylike_minimal runs with SDL3, a pixel-perfect camera, a single-batch 2D renderer, and a generated 8x8 tile atlas. One draw call per frame for quads. Basic audio oscillator plays. Input module integrated. Logic runs in FixedUpdate; rendering in Update.
- scene2d batching supports a texture layer attribute in the vertex (for future sampler2DArray), with a new push_ex API. Shaders currently use sampler2D (atlas) and ignore the layer.
- Example has been switched to glad/gl.h and direct gl* calls after context creation.

Pitfalls encountered (and lessons)
- Multiple definition linker errors
  - Cause: Globals unitylike::g_comp and unitylike::g_comp_script_host defined in multiple TUs (Components.cpp and SceneCore.cpp). Linker reported duplicate symbol definitions.
  - Lesson: Define globals exactly once (one TU) and provide extern declarations in a header. Avoid inline variable linkage surprises.

- Input singleton unresolved externs
  - Cause: Inconsistent removal/references caused missing symbols at link time.
  - Lesson: Search whole repo + CMake for lingering references when deprecating singletons. Replace with the new input module API and wire endpoints before deletion.

- No rendering / empty window
  - Cause: Initial GL setup did not create a shader/VAO/VBO pipeline and textures correctly.
  - Lesson: Ensure a minimal GL pipeline exists: shader program, vertex buffers, correct attribute layouts, and an active texture bound before draw.

- Aspect ratio / stretched sprites
  - Cause: glViewport not set on init/resize/frame, mismatched coordinate mapping.
  - Lesson: Set glViewport on init and window resize; if using pixel-perfect camera, keep MVP and viewport consistent with framebuffer size.

- Timing and threading model (FixedUpdate vs Update)
  - Cause: Logic tied to render framerate caused frame-dependent motion.
  - Lesson: Run gameplay in FixedUpdate with fixed dt; keep Update for rendering-only work. Scale motion by fixed dt.

- Batch rebuild cost
  - Cause: Rebuilding the entire batch object every frame is wasteful.
  - Lesson: Reuse the batch object and update vertex data only. Only rebuild topology when necessary; otherwise just rewrite verts.

- GL function loading inconsistency
  - Cause: Mixing SDL_GL_GetProcAddress usage per function with glad causes complexity and crashes.
  - Lesson: Use glad consistently after context creation; remove per-function lookups.

- Shader uniform design
  - Cause: Using ad-hoc uniform like u_cam vec3 and doing transforms in fragment/vertex awkwardly.
  - Lesson: Use a proper MVP matrix (u_mvp) from a pixel-perfect camera helper (ame_camera_make_pixel_perfect).

- Sampler strategy for many textures
  - Current: Single sampler2D atlas. No array usage yet, though vertex now carries layer.
  - Lesson: To get many independent textures in one draw, move to sampler2DArray or bindless; ensure vertex has layer and shader samples with vec3(uv, layer).

Current implementation details
- scene2d.h
  - AmeVertex2D now includes float l (layer). Default 0.
  - New ame_scene2d_batch_push_ex(..., layer) API; legacy push() keeps layer=0.
  - Batching maintains ranges by texture (GLuint). Finalize closes the current range.

- examples/unitylike_minimal/main.cpp
  - Uses glad GL after SDL context creation: gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress).
  - Simple shader with u_mvp and sampler2D u_tex; a_pos (loc 0), a_col (loc 1), a_uv (loc 2).
  - Pixel-perfect MVP from ame_camera_make_pixel_perfect.
  - Generates an 8x8 atlas (128x128) procedurally, nearest sampling.
  - Builds batch per-frame by rewriting vertex data; one draw call.
  - Gravity, jump, camera follow, and three scripted movers present (logic in FixedUpdate behaviours).

Open issues / Next actions (prioritized)
1) Fix duplicate global definitions
   - Search for unitylike::g_comp and unitylike::g_comp_script_host definitions.
   - Keep one definition in a single .cpp; add extern declarations in a shared header (e.g., unitylike/Scene.h or a dedicated header).
   - Rebuild to confirm no duplicate symbol errors.

2) Verify Input singleton removal
   - Repo-wide grep for old Input singleton references (code and CMake). Remove or replace with input module functions.
   - Rebuild to ensure no unresolved externs.

3) Upgrade rendering to sampler2DArray
   - Add layer attribute to VAO layout (e.g., location=3, 1 float) and populate from AmeVertex2D.l.
   - Shader changes:
     - Vertex: pass layer to fragment (flat float or mediump float).
     - Fragment: sampler2DArray u_tex; sample via texture(u_tex, vec3(v_uv, v_layer)).
   - Update batch code to call ame_scene2d_batch_push_ex with proper layer for each sprite.
   - Provide a texture array creation path (glTexImage3D or glTexStorage3D); migrate atlas or load multiple textures as layers.
   - Keep one draw call with ranges that group by texture array object.

4) Camera follow on Update path
   - Confirm camera target and smoothing are updated on the render thread (Update) only; logic remains in FixedUpdate.

5) Build hygiene
   - Ensure glad is initialized once; remove any lingering manual GL proc loads.
   - Consider splitting GL setup into a helper for reuse.

6) Diagnostics & validation
   - Log draw call count after render; target is one.
   - Add a simple renderdoc-friendly marker or GL debug output toggle when available.

How to build and run
- From repo root:
  - mkdir -p build && cd build
  - cmake .. -DCMAKE_BUILD_TYPE=Debug
  - cmake --build . -j
  - ./examples/unitylike_minimal/unitylike_minimal

Checks after running
- Window opens, sprites visible, movement framerate independent.
- Draw calls reported as 1 on frame 2.
- Resizing window maintains correct aspect (glViewport on resize).
- No linker warnings about duplicate symbols; no unresolved externs for Input.

Search hints
- Duplicate symbols: grep -R "g_comp\>\|g_comp_script_host\>" -n .
- Input singleton: grep -R "\bInput::\|INPUT_SINGLETON\|input_singleton" -n .; also scan CMakeLists.txt.
- GL proc lookups: grep -R "SDL_GL_GetProcAddress\|PFN_gl" -n examples/ unitylike/ include/

Notes for future agents
- Respect the user-edited diffs: if you see the tool output message "This update includes user edits!" you must not undo those edits.
- Keep changes localized and follow the project’s existing patterns.
- Prefer editing via precise diffs; do not truncate code in search/replace patches.

