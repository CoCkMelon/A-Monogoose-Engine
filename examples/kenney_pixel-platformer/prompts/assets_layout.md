Title: Assets Layout and Expected Filenames

Purpose
Define a predictable directory structure and asset names for the pixel platformer example to simplify loading prompts and code.

Base directory
examples/kenney_pixel-platformer/assets/

Recommended structure
- Tiled/
  - tilemap-example-a.tmx
  - tileset_ground.tsx
  - tileset_props.tsx
  - tileset_characters.tsx
  - images/
    - tiles_ground.png
    - tiles_props.png
    - characters.png
- audio/
  - music_time_for_adventure.opus
  - ambient_power_up.opus
  - sfx_jump.opus
- sprites/
  - player_spritesheet.png  (uniform 16x16 grid preferred)
  - player_spritesheet.json (optional mapping of frames to states)

Loading conventions
- TMX path default: examples/kenney_pixel-platformer/Tiled/tilemap-example-a.tmx (original layout). If reorganizing under assets/Tiled/, update prompt paths accordingly.
- TSX <image> source paths may be relative; support ../ to step out of Tiled folder to the assets root when needed.
- Audio file names align with audio_thread_and_sync.md; update constants if assets are renamed.
- Player spritesheet defaults: sprites/player_spritesheet.png; 16x16 frames; state mapping idle=[0], walk=[1,2], jump=[3]. Override via JSON if provided.

Guidance
- Use NEAREST filtering for pixel art textures and CLAMP_TO_EDGE to prevent bleeding.
- Keep tile map tilewidth/tileheight consistent with spritesheet frame size when possible (e.g., 16x16 or 18x18 according to the map).
- If using the original Kenney/Brackeys assets already present, either symlink or duplicate them into assets/ to match these defaults.

