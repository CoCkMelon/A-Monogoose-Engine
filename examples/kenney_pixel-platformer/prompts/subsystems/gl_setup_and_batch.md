Title: Subsystem – GL Setup, Batch Rendering, and Shutdown

Purpose
Specify how to initialize OpenGL, compile/link shaders, configure state for 2D, manage a simple interleaved sprite batch, and free resources on shutdown.

Initialization
- Request GL 4.5 core, triplebuffer, no depth buffer (depth size 0).
- Create SDL window with OPENGL|RESIZABLE; create GL context; load GL via glad. If needed, resize window, if failed crop viewport, to match even number.
- Compile/link two programs:
  1) Batch program (vs_src, fs_src) with attributes: a_pos(0), a_col(1), a_uv(2); uniforms: u_res, u_camera, u_use_tex, u_tex.
  2) Tile program (tile_vs_src, tile_fs_src) for full-screen tile pass with arrays.
- Create VAOs/VBOs:
  - g_vao for general drawing
  - VBOs: g_vbo_pos, g_vbo_col, g_vbo_uv (dynamic for immediate quads)
  - g_batch_vbo (dynamic interleaved AmeVertex2D buffer)
  - g_fullscreen_vao (no buffers; uses gl_VertexID)
- Query uniform locations and cache them.
- GL state:
  - glViewport(0,0,w,h)
  - glClearColor(sky blue)
  - glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
  - glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE)

Sprite Batch (AmeScene2DBatch)
- Data: struct AmeVertex2D { vec2 pos; vec4 col; vec2 uv }
- API:
  - init(): allocate dynamic array
  - reset(): clear verts and ranges
  - append_rect(tex, x,y,w,h, r,g,b,a): pushes 6 interleaved vertices and updates texture ranges
  - finalize(): compute ranges by contiguous texture runs
- Upload/draw:
  - Bind g_batch_vbo; glBufferData(GL_ARRAY_BUFFER, count*sizeof(AmeVertex2D), data, GL_DYNAMIC_DRAW)
  - Setup attribs on g_vao: loc0=pos, loc1=col, loc2=uv with stride=sizeof(AmeVertex2D)
  - glActiveTexture(GL_TEXTURE0); glUniform1i(u_tex, 0)
  - For each draw range: set u_use_tex, bind texture (or 0), glDrawArrays(GL_TRIANGLES, first, count)

Camera Uniform
- u_camera = vec4(cam_x_snapped, cam_y_snapped, zoom, rotation)
- u_res = vec2(w, h)

Shutdown
- Delete buffers: dynamic VBOs, batch VBO
- Delete VAOs: g_vao, g_fullscreen_vao
- Delete programs: tile and batch
- Delete textures created here if owned (player frames, tile atlases if not shared)
- Destroy GL context and window
- Free batch storage

Resize Handling
- On SDL_EVENT_WINDOW_RESIZED: update g_w,g_h, glViewport, and ame_camera_set_viewport(camera,...)

