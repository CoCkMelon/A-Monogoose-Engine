ame-next ENGINE SPEC (plain text, editable)

WHAT THIS IS
A short spec for building a small game engine from scratch that
targets BOTH 2D and 3D from one C core.
An agent or human should be able to rebuild it and get one coherent
engine, not a mess of overlapping layers (that is what killed the two
earlier attempts - see postmortem notes).

CHOICES MADE (fixed - change here only if you must)
  Language:  C23 core (engine + game, ONE standard per build). Third-
             party libs keep their own standard. v0 has NO physics
             library at all - only a geometry/collision-query module
             (pure C). Later projects that need rigid-body physics
             link Box2D/Box3D DIRECTLY (both C by Erin Catto) with no
             wrapper. All public headers use extern "C".
  Dimensions: 2D and 3D from one C core; a build macro (AME_2D or
             AME_3D) picks the math/camera/geometry/render variant.
             Shared systems (pools, events, audio, dialogue, text,
             UI, input) are dimension-agnostic.
  Platform:  TARGETS (first-class): POSIX desktop (Linux, macOS, other
             Unix), Windows, ANDROID, and WEB (Emscripten) - one C
             core, per-target toolchain + thin bootstrap. SDL3 window/
             events/audio; OpenGL ES on Android/web, desktop GL on
             POSIX desktop + Windows. Native low-level input backend
             chosen at COMPILE TIME: asyncinput (raw keyboard/mouse;
             SDL stays for windowing + gamepad) or SDL input.
             Web/Android map browser/OS input instead. All targets are
             built and tested in GitHub Actions CI (build.txt).
  Hot data:  fixed static arrays (pools), SoA preferred. No malloc
             in the game loop.
  Physics:   NONE at v0 - geometry only. A geometry/collision-query
             module (line/shape intersections, raycasts, mesh->
             primitive proxies, broad-phase). Rigid-body physics
             (Box2D/Box3D) is an OPT-IN per later project, linked raw
             with no abstraction. See physics.txt.
  Events:    discrete gameplay events (overlap-edge detection over the
             geometry module + a deferred queue). No general event bus
             yet, no physics solver needed. UI: in-scene mesh + C code.
  Levels:    static geometry + markers + code that spawns actors.
             NO runtime scene graph, NO fancy scene YAML.
  Dialogue:  tight YAML, parsed at runtime with libfyaml (a vendored
             C YAML 1.2 parser). Optional bake-to-C for embedded/web.
  Save/load: game progress saved/loaded as YAML via libfyaml.
  Settings:  full settings screen is DATA (YAML) authored by an AI
             agent and rendered procedurally; libfyaml at runtime.
             See settings.txt.
  Content:   things you edit a lot are either loaded via libfyaml
             (dialogue, saves, settings definitions) or turned into C
             at build time.
  Loop:      split threads (kept): a logic thread steps the sim (and
             geometry) at a fixed 1000 Hz; the main thread renders at
             display rate; cross-thread state via atomics/snapshots.
             At least 1000 Hz simulation (or update-on-input-callback
             for real-time actions) is NON-NEGOTIABLE for low latency;
             see loop.txt.
  Geometry:  model geometry via Assimp (build-time tool) - do not
             hand-write a runtime .obj parser.
  Assets:    ship an assets folder OR embed files into the binary with
             C23 #embed (the engine builds as C23 everywhere). No
             mandated data-pack pipeline. See deploy.txt.
  Distribution: static-link third-party libs (RPATH $ORIGIN on Linux
             when dynamic) so the game runs without dev libs; packages
             per target produced + smoke-tested in GitHub Actions.
             See deploy.txt.

FIRST GAME
  The first game this engine must ship is an ONLINE COMPETITIVE MEMORY
  GAME (two players over the network, taking turns to open cards). It
  is the vertical slice everything is measured against - if it is not
  playable, the rest does not matter. It deliberately MIXES 2D AND 3D
  UI so the v0 engine's single-pass 2D+3D renderer is genuinely
  exercised, not just "proven by a cube":
    - a 2D-style card GRID laid out on a table,
    - each card is a 3D PANEL that ROTATES (flips) open/closed with a
      3D turn (so text/faces are readable on a 3D-quad after flip),
    - 2D-looking scoreboard / whose-turn / UI drawn IN-SCENE as
      billboards over the same single pass (ui.txt).
  RULES (fixed; drive the sim and netcode messages):
    - Two players, strict ALTERNATION of turns (turn passes every
      time regardless of whether a match was found).
    - On your turn you open TWO cards (each flips open with the 3D
      rotation). If they MATCH they stay face-up and you score one
      match; they still count as your turn's two opens and then the
      turn passes.
    - The board is a fixed, even-sized grid of shuffled pairs; all
      pairs found ends the game.
    - WINNER: whoever found MORE matching pairs. Tie allowed.
    - Deterministic sim (no real-time physics; the engine's geometry
      module is still present but the game itself is turn logic +
      animation only).
  This is the anchor. It is online (server-authoritative) from the
  start, so a runnable client/server over loopback is part of the
  first milestone (build.txt / Stage notes below), not a distant add.

IMPLEMENTATION STAGES (roadmap; later stages add advanced features)
  The engine is built in CAPABILITY stages. Each stage compiles, runs,
  is tested in CI, and ends at a defined exit. Order is a proposal -
  reorder freely, but keep the dependency: nothing advanced runs
  before the 2D+3D single-pass core (Stage 0).

  STAGE 0 - Core: 2D+3D single-pass + first game LOCAL (v0)
    Foundations + the single-pass textured renderer serving BOTH 2D
    and 3D from one C core (render.txt first milestone) + enough to
    make the Memory game playable LOCALLY (two sides on one machine /
    a local test driver) with 3D card-flip animation.
    Builds on: core/build, data pools, 1000 Hz fixed-step loop,
    input, GEOMETRY/collision-query module (present but light use;
    no physics, no solver), events, single-pass textured renderer
    (2D sprites AND 3D unlit meshes - the card grid is real 3D panels
    that rotate), text via a font-to-texture GLYPH ATLAS baked at
    build time (DSDF optional), in-scene single-pass UI + billboards,
    audio basics, assets folder or #embed, deterministic per-binary
    turn/game logic. NOTE: dialogue / save-load / settings-YAML and
    libfyaml are NOT part of the first game slice (deferred; the game
    needs none of them). Networking is NOT in this stage's exit.
    EXIT: local Memory game playable (two alternating players, open
    two cards each with 3D flip, matched pairs counted, most matches
    wins, replay with a fixed seed is deterministic); the single-pass
    3D card render is verifiable headless (screenshot); clean build +
    tests; packaged unit runs on a clean machine.

  STAGE 1 - Online (server-authoritative) Memory game
    Make the LOCAL game ONLINE over the network. Server-authoritative
    (principles.txt): a dedicated server owns the authoritative game
    state; each client is a thin view that sends its card-open intents
    and renders what the server broadcasts. Because turns are discrete
    and the sim is deterministic, the protocol is small and testable
    over loopback (127.0.0.1) in CI / headless before any real host.
    Includes: client/server netcode, authoritative server owning the
    board + whose-turn, a simple message set (join/state/your-turn/
    open-card/match/no-match/win), latency tolerance (a card open is
    a single request; no prediction/rollback needed for a turn game),
    reconnection/clean-exit basics.
    EXIT: two clients (two processes, loopback) play a full Memory
    game correctly against one authoritative server; a headless
    scripted two-client test finishes a game and reports the correct
    winner; network is robust to one client dropping.

  STAGE 2 - Advanced rendering + richer 3D (engine capability)
    Multipass and post-processing (the multipass decision in
    render.txt now gets made), offscreen passes, and - for the 3D
    path - lighting, shadows, materials, particles, deeper mesh
    content (still Assimp-imported). Keep the one-renderer core; the
    3D path stops being unlit here. This is where the Memory game's
    board gains polish and where the engine proves it is ready for a
    heavier 3D game.
    EXIT: post-processing + lighting/shadows working; a 3D scene under
    a lit camera with in-scene UI/labels; particles.

  STAGE 3 - Runtime codecs
    Runtime audio/video decoding and streaming (decoded/streamed
    samples and video/cutscenes at runtime, beyond the Stage 0 audio
    "decoded sample" basics and beyond baked/embedded files).
    EXIT: runtime-encoded audio and video play/stream correctly on
    native and web; no hot-path stalls.

  STAGE 4 - Realtime ray tracing
    Optional advanced path where the renderer/hardware supports it,
    built on the Stage 2 multipass pipeline. Use the engine's own
    geometry module's broad-phase (BVH) for the ray queries. This is
    the most niche + costly
    feature, so it is last; it is a capability, not a requirement.
    EXIT: a ray-traced or hybrid-RT scene renders at an acceptable
    frame rate on capable hardware; clean fallback without RT.

  STAGE 5 - Breadth, tooling, polish
    Job system / more threading, async asset streaming, animation
    (2D spritesheets + 3D), editor/debug tooling, profiling, broader
    localization, deeper settings wiring, per-platform determinism,
    distribution hardening. Pure additive; nothing architectural.
    EXIT: engine is comfortable for a second, larger project.

  HOW AGENTS BUILD IT (ties to agents.txt)
    Within each stage: build the isolated pure modules first
    (0-shot-able: pools, layout, synth, codegen), then run the
    iterative INTEGRATION phase behind a small running harness for
    the visual/geometry/netcode parts. Do not try to build a whole
    stage in one agent pass.

THE ONE RULE (remember this above all)
Two kinds of object exist, and they follow different rules:
  1. SETUP objects - built once at startup (a render pass, a camera,
     an audio source config). Use a builder that takes a pointer,
     mutates in place, and returns the same pointer. Cheap to chain,
     no copies, fine to use.
  2. HOT state - entities, bullets, positions, live audio, updated
     every step. These live in fixed static pools. NEVER wrap them
     in builders/objects/classes. Just arrays and plain functions.

NEVER AGAIN (banned - these exact things failed before)
  - No ECS as a core dependency.
  - No Unity-style GameObject/Component facade layer.
  - No second copy of a feature in another language (no TS port,
    no hand-written WASM twin). One C implementation per feature.
  - No hot-path allocation.
  - No scene file that stores logic/rules at runtime.
  - No huge benchmark/prose docs; if you claim speed, ship the code.

FILES IN THIS FOLDER
  README.txt       this file + first game + implementation stages
  principles.txt   the ideas and rules
  data.txt         data model and pools
  loop.txt         app entry + game loop
  render.txt       drawing (single-pass textured, 2D+3D at v0; UI
                   single-pass; multipass later)
  physics.txt      geometry at v0; raw Box2D/Box3D (opt-in) later
  audio.txt        sound
  input.txt        controls
  levels.txt       levels/content (Assimp import; assets folder or
                   embed)
  text.txt         text (glyph atlas meshes at v0; DSDF optional)
  events.txt       discrete gameplay events (overlap-edge + deferred
                   queue)
  ui.txt           UI scope (in-scene mesh, single-pass)
  settings.txt     data-driven Settings (procedural, AI-authored YAML)
  dialogue.txt     dialogue (tight YAML, libfyaml runtime)
  save.txt         save/load games (YAML via libfyaml)
  deploy.txt       distribution per target (POSIX/Windows/Android/web;
                   assets folder or embed; static libs)
  build.txt        build + tests (GitHub Actions cross-platform CI)
  agents.txt       rules for whoever edits this
  reference/       source documents the specs derive from + CI notes:
                   settings-taxonomy-2d3d.txt,
                   sdl3-fast-build-recipe.txt (headless SDL3 for CI)

NOTE: the external engine review lives OUTSIDE this spec, at the
workspace/project root as REVIEW_ame-next-spec.md.
