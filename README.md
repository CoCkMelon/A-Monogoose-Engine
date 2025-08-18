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

Notes
- Mirrors SDL3 callback style (SDL_AppInit/Event/Iterate/Quit) similar to asyncinput/examples/sdl3_clear.c.
- Uses asyncinput (../asyncinput) as a subdirectory build and links against it.
- Uses OpenGL 4.5 core profile and draws a GL_LINE_STRIP of appended vertices.
