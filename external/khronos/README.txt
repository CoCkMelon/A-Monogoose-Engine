Vendored Khronos OpenGL registry headers.

Why these are in the tree
-------------------------
src/render.c needs the OpenGL core-profile typedefs and PFNGL*PROC prototypes
(GLuint, GLenum, PFNGLCREATESHADERPROC, ...) but must NOT include anything from
SDL3: the renderer takes an injected proc getter so that app_sdl.c owns every
SDL symbol and render.c stays platform-agnostic. That is a standing boundary in
this project, and CI greps examples/ for SDL_ to hold the game side to it.

Every other target gets those declarations from its own toolchain:
  Linux     /usr/include/GL/glcorearb.h        (libgl-dev)
  Android   <GLES3/gl3.h> in the NDK sysroot   (AME_GL_DIRECT path)
  web       <GLES3/gl3.h> from emscripten      (AME_GL_DIRECT path)
  macOS     <OpenGL/gl3.h>                     (Apple's, AME_GL_DIRECT path)
  MinGW     the cross sysroot ships GL/         (windows-cross job, green)

Native Windows with LLVM/MSVC is the one gap: the runner has no Khronos headers
at all, only SDL3's own SDL_opengl.h, so the windows (clang C23 compile) job
failed with "fatal error: 'GL/glcorearb.h' file not found" once the earlier
configure failures were fixed and it finally reached render.c. Using SDL3's
header would break the SDL-free property above, so the Khronos header is
vendored instead and used on that one target.

Provenance and licence
----------------------
Both files are generated from the Khronos OpenGL / OpenGL ES XML API Registry,
https://github.com/KhronosGroup/OpenGL-Registry

  GL/glcorearb.h    Copyright 2013-2020 The Khronos Group Inc.
                    SPDX-License-Identifier: MIT
  KHR/khrplatform.h Copyright (c) 2008-2018 The Khronos Group Inc.
                    SPDX-License-Identifier: MIT

Copied verbatim from the Debian libgl-dev package (glcorearb.h dated
2023-09-12, 425985 bytes; khrplatform.h 11131 bytes). No edits. glcorearb.h
includes <KHR/khrplatform.h>, which is why both are here, and it includes
<windows.h> itself under _WIN32 for APIENTRY, defining WIN32_LEAN_AND_MEAN
first.

MIT matches the licence of the other vendored code in external/ (stb), and the
same rule applies: files under external/ are exempt from the engine's
warnings-as-errors (build.txt rule 3), because they are not our code.

Do not hand-edit these. Re-copy them from the registry if a newer GL version is
ever needed, and record the new provenance above.
