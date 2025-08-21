A Mongoose Engine - SDL3 + OpenGL 4.5 + asyncinput

This is a minimal 2D engine scaffold using SDL3 for windowing + OpenGL 4.5 for rendering, and libasyncinput for high-performance input (no SDL input path for mouse).

Example app: curve painting. Hold left mouse button and move to draw a continuous line. Mouse input is handled via asyncinput callback that accumulates relative motion and appends vertices.

Requirements
- SDL3 development package
- OpenGL 4.5 capable driver
- CMake >= 3.16 and a C compiler
- Permissions to read /dev/input/event* (e.g., run as a user in input group or via appropriate udev rules)

Building

mkdir -p build && cd build
cmake ..
cmake --build . -j

Running

From the build directory:
./curve_paint

Roadmap
See docs/ROADMAP.md for the long-term plan and the current short-term focus on exploring gameplay without scene files or physics.

Architecture overview
- SDL3 for windowing, timing, and app lifecycle; OpenGL 4.5 core for rendering.
- libasyncinput provides low-latency input; flecs powers ECS where used.
- Box2D (via a C++ bridge) provides physics for examples that need it.
- Audio mixer with optional spatialization helper and Opus asset decoding.
- Threads: main (render/event), logic (ECS/physics), audio (mixer sync); state shared via atomics.

More details: docs/ARCHITECTURE.md

Notes
- Mirrors SDL3 callback style (SDL_AppInit/Event/Iterate/Quit) similar to asyncinput/examples/sdl3_clear.c.
- Uses asyncinput (../asyncinput) as a subdirectory build and links against it.
- Uses OpenGL 4.5 core profile and draws a GL_LINE_STRIP of appended vertices.

Guidelines for AI agents

- Logging policy:
  - Wrap non-essential logging behind a DEBUG-only macro. Example in C:
    
        #ifdef DEBUG
        #define LOGD(...) SDL_Log(__VA_ARGS__)
        #else
        #define LOGD(...) ((void)0)
        #endif
    
  - Convert diagnostic logs to use LOGD and avoid runtime spam in Release builds.
  - Remove unnecessary logs like per-keypress event spam and frequent state dumps.
- String formatting in C code:
  - Use plain ASCII text only. Do not include special Unicode, HTML, or nonstandard escape sequences in source code, but you can do that in string literals.
- Scope of changes:
  - Prefer localized, minimal edits; respect existing code style and interfaces.
  - When editing examples, keep behavior identical in Release builds (no extra logging/output).
- Commits by AI:
  - Use clear messages. If requested by the user, note that changes were made by GPT-5 and Claude 4.1 Opus.
