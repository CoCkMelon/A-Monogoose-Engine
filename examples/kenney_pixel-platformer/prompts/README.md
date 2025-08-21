AME Pixel Platformer – Prompt Pack

This folder contains prompt .md files that describe the ECS design of the Kenney pixel platformer example. Use them to (re)generate a clean implementation when the current C example is broken.

Files
- components.md: All ECS components and tags with field specs and intent.
- resources.md: Non-ECS resources and global state to expose to systems.
- systems/*.md: One prompt per system with contract, inputs/outputs, and pseudocode.
- example_assembly.md: A guided prompt to assemble the world: register, instantiate, and run.

Subsystem prompts (under prompts/subsystems/)
- tilemap_and_tilepass.md – TMX/TSX parsing, per-layer GID textures, full-screen tile compositing pass
- gl_setup_and_batch.md – GL init, shader programs, 2D state, sprite batching, resize, shutdown
- input_backend.md – async input initialization, key mapping to atomics, shutdown
- physics_and_collision.md – physics world creation, tilemap collisions, fixed timestep loop
- audio_thread_and_sync.md – audio init, fast thread, atomic parameters, source synchronization
- spritesheets_and_animation.md – load player animations from spritesheets, UVs or sliced textures, fallback

How to use (recommended order)
1) Define data: components.md and resources.md
2) Initialize platform and rendering: gl_setup_and_batch.md
3) Load content: tilemap_and_tilepass.md and spritesheets_and_animation.md
4) Initialize IO and subsystems: input_backend.md, physics_and_collision.md, audio_thread_and_sync.md
5) Implement gameplay systems: systems/*.md
6) Wire everything: example_assembly.md

Notes
- Use this as a source of truth if you need to re-generate platform-specific glue (SDL/GL) later.
- You can substitute the spritesheet loader with the procedural textures described in spritesheets_and_animation.md if assets are missing.

