Title: Subsystem – Tilemap Loading and Full-Screen Tile Pass

Purpose
Define how to parse TMX/TSX, load tilesets and atlas textures, construct per-layer GID integer textures, and render all layers in a single full-screen pass.

Scope
- TMX parsing: map size, tile size, layers with CSV-encoded data
- TSX parsing: tilecount, columns, tile width/height, image path and size
- Tileset selection per layer and GID normalization
- GL textures: atlas textures (sampler2D) and per-layer gid textures (isampler2D, R32I)
- Shader interface: uniforms and texture bindings for up to 16 layers

Data Structures
- ParsedTileset { int firstgid; AmeTilesetInfo ts; string image_path; GLuint atlas_tex }
- TileLayerRender (per layer)
  - AmeTilemap map
  - GLuint atlas_tex
  - int atlas_w, atlas_h
  - int columns
  - int firstgid
  - GLuint gid_tex (R32I)
  - bool is_collision

Algorithm: Load TMX and TSX
1) Read TMX file into memory.
2) Read top-level width, height, tilewidth, tileheight.
3) For each <tileset firstgid=... source="foo.tsx"> element:
   - Build absolute path to TSX relative to TMX dir.
   - Parse TSX: tilecount, columns, tilewidth/height, <image source width height>.
   - Build absolute image path (handle ../ prefix).
   - Load atlas image with SDL_image; upload GL RGBA8 texture with NEAREST filtering and CLAMP_TO_EDGE.
   - Store ParsedTileset with firstgid, ts, image_path, atlas_tex.
   - Ensure ts.columns is set if missing: image_width / tile_width.
   - Sort parsed tilesets by firstgid ascending.

4) For each <layer ...> with <data encoding="csv">:
   - Determine layer width/height (override or map defaults).
   - Parse CSV GIDs. Mask out Tiled flip bits if present; keep raw gid (0=empty).
   - Pick a tileset for the layer by counting gids that fall into each [firstgid, next_firstgid) range; choose the best.
   - Zero out gids that are outside the chosen tileset range for normalization.
   - Create TileLayerRender L:
     - map.width=lw; map.height=lh; map.tile_width=map_tw; map.tile_height=map_th
     - map.tileset = chosen.ts; map.tileset.firstgid = chosen.firstgid
     - map.layer0 = { width=lw, height=lh, data = parsed_gid_array }
     - atlas_tex = chosen.atlas_tex; firstgid = chosen.firstgid; columns = chosen.ts.columns
     - atlas_w = chosen.ts.image_width; atlas_h = chosen.ts.image_height
   - Create gid_tex:
     - GL_TEXTURE_2D; internalFormat=GL_R32I; format=GL_RED_INTEGER; type=GL_INT
     - Size = lw x lh; data = int32 gid array
     - Sampling: MIN/MAG NEAREST; wrap CLAMP_TO_EDGE
   - Heuristic collision layer: the first non-empty layer whose name contains "Tiles"; fallback to first non-empty layer.

Shader Interface (Full-Screen Tile Pass)
- Vertex shader: 3-vertex fullscreen triangle; pass v_uv = (p+1)/2; no VBO needed
- Fragment shader: Sample tiles from tilemap and blend layers.

Binding and Draw
- Set u_res and u_camera (with snapped camera x,y for pixel-perfect).
- Set u_map_size and u_tile_size from first layer.
- Set u_layer_count = min(layer_count, 16).
- Prepare arrays for texture units: atlas_units[i] = 1+i; gid_units[i] = 17+i
- glUniform1iv for u_atlas and u_gidtex arrays.
- glUniform1iv/2iv for u_firstgid, u_columns, u_atlas_tex_size.
- Bind each layer's atlas to GL_TEXTURE0+atlas_units[i] and gid_tex to GL_TEXTURE0+gid_units[i].
- Bind fullscreen VAO; glDrawArrays(GL_TRIANGLES, 0, 3).

Notes
- CSV read must strip whitespace and commas, tolerate short data by zero-filling.
- Preserve the raw data buffer for physics collision build.
- Atlas textures may be shared by layers; avoid double-deleting on shutdown.

