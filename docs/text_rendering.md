# Text Rendering Plan

Current status
- Using SDL_ttf in examples where needed (simple text UI). This is acceptable for prototypes but has limitations in quality and performance at scale.

Target architecture (future)
- Font loading: FreeType for high-quality glyph outlines.
- Shaping: HarfBuzz for robust shaping (ligatures, kerning, complex scripts, bi-directional text).
- Rendering: MSDF (multi-channel signed distance fields) glyph atlases with a single MSDF shader.
  - Crisp edges across a wide range of sizes.
  - Batch thousands of glyphs with minimal draw calls.
  - Atlas growth with on-demand glyph rasterization.
- Layout: per-frame or cached glyph runs computed via HarfBuzz (word-wrapping, alignment), exposed to renderer as batches.

Pipeline outline
1) Font cache (per font id)
   - Load FT_Face from FreeType.
   - On demand, build MSDF bitmap for a glyph, pack into atlas, store UVs and metrics.
2) Shaping and layout
   - Use HarfBuzz to shape UTF-8 text into glyph indices and positions.
   - Apply wrap width and line breaks; produce glyph runs.
3) Rendering
   - Single pass MSDF shader; color from component; snap if needed for pixel-perfect UI.
   - Batching for all text in the frame.

Façade integration
- TextRenderer component (façade) provides: text_ptr (engine-managed heap string), font (id), color (vec4), size (px), wrapWidth (px), plus request_buf/request_set for updates from C++.
- Engine text systems:
  - ApplyRequests: copy façade request_buf into heap string for text_ptr.
  - BuildLayout: shape/layout into glyph runs (future), then hand to renderer/batcher.

Why not SDL_ttf or stb_truetype directly?
- SDL_ttf: convenient but rasterizes per size and often per draw, can be CPU heavy; low-level control over atlases and shaping is limited.
- stb_truetype: fast for basic text but lacks shaping and high-quality hinting; needs a lot of bespoke work for internationalization.
- FreeType + HarfBuzz + MSDF: combines best-in-class quality and performance with robust international language support.

Transition plan
- Keep SDL_ttf for now in examples.
- Introduce text_system.c (ApplyRequests now) and later add font cache, shaping, and MSDF renderer incrementally.
