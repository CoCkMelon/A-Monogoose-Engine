Title: Subsystem – Spritesheets and Player Animation Loading

Purpose
Load player animations from spritesheets instead of procedural textures, slice frames, and map them to animation states used by the animation system.

Input Assets
- A spritesheet image (PNG) containing frames for idle, walk, jump
- Optionally a JSON or simple config describing frame rectangles per state; or assume uniform grid

Approach A: Uniform Grid Spritesheet
- Parameters:
  - path: "assets/player_spritesheet.png"
  - frame_w, frame_h (e.g., 16x16)
  - rows/cols or total_frames
  - state ranges: idle=[0], walk=[1,2], jump=[3]
- Steps:
  1) Load surface via SDL_image; convert to RGBA32
  2) Create a GL parent texture with the full sheet, or slice into separate GL textures per frame
     - Option 1 (preferred): keep full atlas and render using UVs per frame in the batch
     - Option 2 (simple): glTexSubImage2D into separate textures for each frame (more state changes)
  3) If using full atlas:
     - Store spritesheet_tex (GLuint) and frame rects in pixels for each frame
     - Modify batch append to accept a frame index and compute UVs accordingly
  4) If using separate textures:
     - Allocate player[4] as in main.c but filled by sub-rect uploads

Approach B: JSON-described Frames
- JSON schema example:
  {
    "image": "assets/player.png",
    "frame_w": 16, "frame_h": 16,
    "states": {
      "idle": [0],
      "walk": [1,2],
      "jump": [3]
    }
  }
- Steps:
  1) Parse JSON (or a simple ad-hoc format) to get mapping
  2) Proceed as in Approach A using either full-atlas UVs or sliced textures

Batch Integration
- If using atlas + UV frames:
  - Extend AmeScene2DBatch API to append_rect_with_uv(tex, x,y,w,h, u0,v0, u1,v1, color)
  - For frame index f with rect (px,py,fw,fh):
    - u0=px/atlas_w, v0=py/atlas_h, u1=(px+fw)/atlas_w, v1=(py+fh)/atlas_h
  - Keep camera snapping identical
- If keeping separate textures per frame:
  - Maintain CTextures.player[4] as before

Animation State Mapping
- The Animation system remains the same (frames: 0=idle, 1..2=walk, 3=jump)
- Map these indices to either UV rects or GL texture IDs
- If more frames exist (e.g., longer walk), adjust mapping and timing accordingly

Fallback
- If the spritesheet is missing or fails to load, fall back to procedural 16x16 player textures used previously

Shutdown
- Delete spritesheet texture (and any subframe textures if sliced)
- Clear any dynamically allocated frame rect arrays

