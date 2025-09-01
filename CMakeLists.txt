# Enforce out-of-source builds
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "
  FATAL: In-source builds are not allowed.
  Please create a separate build directory:
      mkdir build
      cd build
      cmake ..
  ")
endif()

cmake_minimum_required(VERSION 3.16)
project(a_mongoose_engine C CXX)

option(AME_BUILD_EXAMPLES "Build engine example programs" OFF)
option(AME_WITH_FLECS "Build with Flecs ECS integration" ON)
option(AME_BUILD_UNITYLIKE "Build C++ unity-like facade (requires Flecs)" ON)
# Prefer static variants of SDL3, SDL3_image, SDL3_ttf when available (default OFF as SDL3 static is large)
option(AME_PREFER_STATIC_SDL "Prefer linking against static SDL3/SDL3_image/SDL3_ttf if available" OFF)
# If enabled, fetch and build static SDL3/SDL3_image/SDL3_ttf when static targets are not found
option(AME_FETCH_STATIC_SDL "Fetch and build static SDL3/SDL3_image/SDL3_ttf if static targets are missing" OFF)
# If enabled, force using static SDL; will fetch if not available
option(AME_FORCE_STATIC_SDL "Force static SDL; fetch and build static SDL if not available" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OpenGL, Threads, PkgConfig
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# Decide whether to fetch SDL components first to avoid alias collisions
include(FetchContent)
if(AME_FORCE_STATIC_SDL)
  set(AME_PREFER_STATIC_SDL ON CACHE BOOL "" FORCE)
endif()

# Internal flags to control whether we will fetch these components
set(_AME_FETCH_SDL3 OFF)
set(_AME_FETCH_SDL3_IMAGE OFF)
set(_AME_FETCH_SDL3_TTF OFF)

if(AME_FORCE_STATIC_SDL)
  set(_AME_FETCH_SDL3 ON)
  set(_AME_FETCH_SDL3_IMAGE ON)
  set(_AME_FETCH_SDL3_TTF ON)
elseif(AME_PREFER_STATIC_SDL AND AME_FETCH_STATIC_SDL)
  # We'll fetch when static targets are missing; to avoid alias collisions with system packages,
  # skip find_package for these and rely on fetched projects to define the targets.
  set(_AME_FETCH_SDL3 ON)
  set(_AME_FETCH_SDL3_IMAGE ON)
  # TTF is only needed with examples; decide later
endif()

# If fetching, disable system packages so they don't define conflicting imported targets
if(_AME_FETCH_SDL3_IMAGE)
  set(CMAKE_DISABLE_FIND_PACKAGE_SDL3_image ON)
endif()

# Now either fetch or find SDL3
if(_AME_FETCH_SDL3)
  message(STATUS "Fetching SDL3 source (static available)")
  # Build shared+static and minimize subsystems to what this project needs
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  # Disable unused subsystems
  set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
  set(SDL_RENDER OFF CACHE BOOL "" FORCE)
  set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
  set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
  set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
  set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
  set(SDL_POWER OFF CACHE BOOL "" FORCE)
  set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
  set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
  set(SDL_GPU OFF CACHE BOOL "" FORCE)
  # Keep desktop video backends and OpenGL; turn off GLES/Vulkan if not needed
  set(SDL_OPENGL ON CACHE BOOL "" FORCE)
  set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
  set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
  # Avoid enabling optional audio backends explicitly
  set(SDL_ALSA OFF CACHE BOOL "" FORCE)
  set(SDL_PIPEWIRE OFF CACHE BOOL "" FORCE)
  set(SDL_PULSEAUDIO OFF CACHE BOOL "" FORCE)
  set(SDL_SNDIO OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    sdl3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.2
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(sdl3)
else()
  find_package(SDL3 REQUIRED)
endif()

# SDL_image fetch or find
if(_AME_FETCH_SDL3_IMAGE)
  message(STATUS "Fetching SDL3_image source (static or shared based on BUILD_SHARED_LIBS)")
  # SDL_image decides shared/static from BUILD_SHARED_LIBS. To link consistently with static SDL,
  # force SDL_image static by temporarily setting BUILD_SHARED_LIBS=OFF.
  set(_AME_OLD_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
  if(AME_FORCE_STATIC_SDL OR AME_PREFER_STATIC_SDL)
    set(BUILD_SHARED_LIBS OFF)
  endif()
  set(SDL3IMAGE_VENDORED OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    sdl3_image
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
    GIT_TAG release-3.2.0
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(sdl3_image)
  # Restore prior setting for the rest of the project
  set(BUILD_SHARED_LIBS ${_AME_OLD_BUILD_SHARED_LIBS})
else()
  find_package(SDL3_image REQUIRED)
endif()

# Select SDL targets (prefer static when available)
set(AME_SDL3_TARGET SDL3::SDL3)
if(AME_PREFER_STATIC_SDL AND TARGET SDL3::SDL3-static)
  set(AME_SDL3_TARGET SDL3::SDL3-static)
endif()

set(AME_SDL3_IMAGE_TARGET SDL3_image::SDL3_image)
if((AME_FORCE_STATIC_SDL OR AME_PREFER_STATIC_SDL) AND TARGET SDL3_image::SDL3_image-static)
  set(AME_SDL3_IMAGE_TARGET SDL3_image::SDL3_image-static)
endif()

# Default for TTF (will be overridden if examples are enabled and SDL3_ttf is found)
set(AME_SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf)
# Track whether TTF was fetched to avoid alias collisions
set(_AME_FETCHED_TTF OFF)

# Vendored glad
add_library(glad STATIC third_party/glad/src/gl.c)
target_include_directories(glad PUBLIC third_party/glad/include)

# Flecs ECS (optional)
if(AME_WITH_FLECS)
  add_subdirectory(third_party/flecs)
endif()

# Box2D Physics (using FetchContent for cleaner integration)
include(FetchContent)
FetchContent_Declare(
  box2d
  GIT_REPOSITORY https://github.com/erincatto/box2d.git
  GIT_TAG v2.4.1
  GIT_SHALLOW TRUE
)
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(box2d)

# Bring in asyncinput: prefer local sibling checkout if present, otherwise FetchContent
set(ASYNCINPUT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(_LOCAL_ASYNCINPUT_DIR "${CMAKE_CURRENT_LIST_DIR}/../asyncinput")
if(EXISTS "${_LOCAL_ASYNCINPUT_DIR}/CMakeLists.txt")
  message(STATUS "Using local asyncinput at ${_LOCAL_ASYNCINPUT_DIR}")
  add_subdirectory("${_LOCAL_ASYNCINPUT_DIR}" "${CMAKE_BINARY_DIR}/asyncinput_local")
  set(asyncinput_SOURCE_DIR "${_LOCAL_ASYNCINPUT_DIR}")
else()
  include(FetchContent)
  FetchContent_Declare(
    asyncinput
    GIT_REPOSITORY https://github.com/cockMelon/asyncinput.git
    GIT_TAG main
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(asyncinput)
endif()

# Public headers
include_directories(
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${asyncinput_SOURCE_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/third_party
)

# Engine objects (initial modules)
add_library(ame STATIC
    src/camera.c
    src/gl_loader.c
    src/dialogue.c
    src/embedded_dialogues.c
    src/story_route.c
    src/tilemap.c
    src/tilemap_tmx.c
    src/render_pipeline.c
    src/audio.c
    src/physics.cpp
    src/audio_ray.c
    src/text_system.c
)
# Flecs-dependent sources
if(AME_WITH_FLECS)
  target_sources(ame PRIVATE
    src/render_pipeline_ecs.cpp
    src/ecs.c
    src/collider2d_system.c
    src/obj_tinyobj.cpp
  )
endif()
# Ensure required POSIX and math macros on glibc (M_PI, clock_gettime, etc.)
target_compile_definitions(ame PRIVATE _GNU_SOURCE AME_USE_TINYOBJLOADER=1)

# If generated dialogues are present, compile them and enable the macro
if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/src/embedded_dialogues_gen.c)
    target_sources(ame PRIVATE src/embedded_dialogues_gen.c)
    target_compile_definitions(ame PRIVATE AME_HAVE_GENERATED_DIALOGUES=1)
    target_include_directories(ame PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
endif()

# SDL/OpenGL for engine (GL loader needs it)
if(TARGET OpenGL::GL)
    target_link_libraries(ame PUBLIC OpenGL::GL)
else()
    target_link_libraries(ame PUBLIC GL)
endif()

target_link_libraries(ame PUBLIC ${AME_SDL3_TARGET})

# Link vendored glad
target_link_libraries(ame PUBLIC glad box2d)
if(AME_WITH_FLECS)
  target_link_libraries(ame PUBLIC flecs)
endif()

# Audio dependencies via pkg-config
pkg_check_modules(PORTAUDIO REQUIRED IMPORTED_TARGET portaudio-2.0)
pkg_check_modules(OPUSFILE REQUIRED IMPORTED_TARGET opusfile)

target_link_libraries(ame PUBLIC PkgConfig::PORTAUDIO PkgConfig::OPUSFILE ${AME_SDL3_IMAGE_TARGET})

# Link math library where needed (Linux)
if(UNIX AND NOT APPLE)
    target_link_libraries(ame PUBLIC m)
endif()

target_include_directories(ame PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include ${CMAKE_CURRENT_LIST_DIR}/cpp)
# tinyobjloader is header-only; expose its include directory to the engine
if(tinyobjloader_SOURCE_DIR)
  target_include_directories(ame PRIVATE ${tinyobjloader_SOURCE_DIR})
endif()

# --------------------------
# Unity-like C++ façade lib (optional, requires Flecs)
# --------------------------
if(AME_BUILD_UNITYLIKE AND AME_WITH_FLECS)
# Fetch glm (header-only) for the façade without adding as a subproject
include(FetchContent)
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 0.9.9.8
  GIT_SHALLOW TRUE
)
FetchContent_Populate(glm)

add_library(unitylike STATIC
    cpp/unitylike/Components.cpp
    cpp/unitylike/SceneCore.cpp
    cpp/unitylike/Transform.cpp
    cpp/unitylike/TransformHierarchy.cpp
    cpp/unitylike/Rigidbody2D.cpp
    cpp/unitylike/Material.cpp
    cpp/unitylike/SpriteRenderer.cpp
    cpp/unitylike/TilemapRenderer.cpp
    cpp/unitylike/MeshRenderer.cpp
    cpp/unitylike/TextRenderer.cpp
    cpp/unitylike/Collider2D.cpp
    cpp/unitylike/CameraFacade.cpp
    cpp/unitylike/Time.cpp
)

# Expose includes (cpp/ for headers, include/ for C APIs, asyncinput for input keys, glm)
if(glm_SOURCE_DIR)
  target_include_directories(unitylike PUBLIC ${glm_SOURCE_DIR})
endif()

target_include_directories(unitylike PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/cpp
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${asyncinput_SOURCE_DIR}/include
)

target_link_libraries(unitylike PUBLIC ame flecs)
endif()

# Ensure story route header is public
install(FILES include/ame_story_route.h DESTINATION include)

# ==============================
# Examples (optional; default OFF)
# ==============================
if(AME_BUILD_EXAMPLES)
  # text_editor and dialogue_ui_example require SDL3_ttf
  # If we decided to fetch SDL, also fetch TTF to avoid conflicts
  if(AME_FORCE_STATIC_SDL OR (AME_PREFER_STATIC_SDL AND AME_FETCH_STATIC_SDL))
    set(CMAKE_DISABLE_FIND_PACKAGE_SDL3_ttf ON)
    message(STATUS "Fetching SDL3_ttf source (static or shared based on BUILD_SHARED_LIBS)")
    # SDL_ttf also keys off BUILD_SHARED_LIBS; force static to match SDL core choice
    set(_AME_TTF_OLD_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
    if(AME_FORCE_STATIC_SDL OR AME_PREFER_STATIC_SDL)
      set(BUILD_SHARED_LIBS OFF)
    endif()
    set(SDL3TTF_VENDORED OFF CACHE BOOL "" FORCE)
    include(FetchContent)
    FetchContent_Declare(
      sdl3_ttf
      GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
      GIT_TAG main
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(sdl3_ttf)
    # Restore
    set(BUILD_SHARED_LIBS ${_AME_TTF_OLD_BUILD_SHARED_LIBS})
    set(_AME_FETCHED_TTF ON)
  else()
    find_package(SDL3_ttf REQUIRED)
  endif()

  # Prefer static TTF when available
  if((AME_FORCE_STATIC_SDL OR AME_PREFER_STATIC_SDL) AND TARGET SDL3_ttf::SDL3_ttf-static)
    set(AME_SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf-static)
  else()
    set(AME_SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf)
  endif()

  # Example: line_draw
  add_executable(line_draw examples/line_draw/main.c)

  # Example: rhythm_game
  add_executable(rhythm_game examples/rhythm_game/main.c)

  # Example: raymarch_arcade
  add_executable(raymarch_arcade examples/raymarch_arcade/main.c)

  # Example: text_editor (requires SDL3_ttf)
  option(AME_TEXT_EDITOR_USE_ASYNCINPUT "Build text_editor using asyncinput backend (defines AME_USE_ASYNCINPUT)" OFF)
  add_executable(text_editor examples/text_editor/main.c)
  if(AME_TEXT_EDITOR_USE_ASYNCINPUT)
      target_compile_definitions(text_editor PRIVATE AME_USE_ASYNCINPUT=1)
  endif()

  # Example: dialogue_example (console-style)
  add_executable(dialogue_example examples/dialogue_example/main.c)

  # Example: dialogue_ui_example (OpenGL + SDL3_ttf)
  add_executable(dialogue_ui_example examples/dialogue_ui_example/main.c)

  # Example: flecs_scene (interactive ECS scene)
  if(AME_WITH_FLECS)
    add_executable(flecs_scene examples/flecs_scene/main.c)
  endif()

  # Example: flecs_script_scene (script-defined scene rendered with GL)
  if(AME_WITH_FLECS)
    add_executable(flecs_script_scene examples/flecs_script_scene/main.c)
  endif()

  # Example: obj_loading (OBJ importer, 2D rendering via ECS pipeline)
  if(AME_WITH_FLECS)
    add_subdirectory(examples/obj_loading)
  endif()

  # Example: tilemap_render (Tiled JSON tilemap rendering)
  add_executable(tilemap_render examples/tilemap_render/main.c)

  # Example: pixel_platformer (Kenney pixel platformer game)
  add_executable(pixel_platformer examples/kenney_pixel-platformer/main.c)

  # Example: audio panning with ECS-controlled source
  add_executable(audio_pan_example examples/audio_pan_example/main.c)

  # Example: audio opus playback from file
  add_executable(audio_opus_example examples/audio_opus_example/main.c)

  # Example: audio ray tracing with Box2D occlusion
  add_executable(audio_ray_example examples/audio_ray_example/main.c)

  # memory_game_cpp example removed (Input singleton eliminated)

  # ------------------------------
  # Example: unitylike_minimal demo
  # ------------------------------
  add_subdirectory(examples/unitylike_minimal)

  # ------------------------------
  # Example: unitylike_platformer_ecs (Unity-like with ECS rendering)
  # ------------------------------
  if(AME_BUILD_UNITYLIKE AND AME_WITH_FLECS)
    add_subdirectory(examples/unitylike_platformer_ecs)
  endif()

  # ------------------------------
  # Example: unitylike_textured_ground (Unity-like, build ground from textures)
  # ------------------------------
  # Disabled/removed per request
  # add_subdirectory(examples/unitylike_textured_ground)

  # ------------------------------
  # Example: unitylike_box2d_car (Unity-like, Box2D car sidescroller)
  # ------------------------------
  if(AME_BUILD_UNITYLIKE AND AME_WITH_FLECS)
    add_subdirectory(examples/unitylike_box2d_car)
  endif()

  # ------------------------------
  # Example: parent/child transforms demo
  # ------------------------------
  if(AME_WITH_FLECS)
    add_subdirectory(examples/example_parent_child)
  endif()

  # ------------------------------
  # Example: Flecs Script loader
  # ------------------------------
  if(AME_WITH_FLECS)
    add_executable(scene_script_example examples/scene_file/scene_script_example.c)
  endif()

  # Link script/json addons via flecs
  if(AME_WITH_FLECS)
    if (TARGET flecs)
      target_link_libraries(scene_script_example PRIVATE flecs)
    else()
      target_link_libraries(scene_script_example PRIVATE flecs_static)
    endif()
  endif()

  # Prefer the static lib if enabled, fall back to shared otherwise
  if(TARGET asyncinput_static)
      target_link_libraries(line_draw PRIVATE asyncinput_static)
      target_link_libraries(rhythm_game PRIVATE asyncinput_static)
      target_link_libraries(raymarch_arcade PRIVATE asyncinput_static)
      target_link_libraries(dialogue_example PRIVATE asyncinput_static)
      target_link_libraries(dialogue_ui_example PRIVATE asyncinput_static)
      target_link_libraries(flecs_scene PRIVATE asyncinput_static)
      target_link_libraries(flecs_script_scene PRIVATE asyncinput_static)
      target_link_libraries(tilemap_render PRIVATE asyncinput_static)
      target_link_libraries(pixel_platformer PRIVATE asyncinput_static)
      target_link_libraries(audio_pan_example PRIVATE asyncinput_static)
      target_link_libraries(audio_ray_example PRIVATE asyncinput_static)
  elseif(TARGET asyncinput_shared)
      target_link_libraries(line_draw PRIVATE asyncinput_shared)
      target_link_libraries(rhythm_game PRIVATE asyncinput_shared)
      target_link_libraries(raymarch_arcade PRIVATE asyncinput_shared)
      target_link_libraries(dialogue_example PRIVATE asyncinput_shared)
      target_link_libraries(dialogue_ui_example PRIVATE asyncinput_shared)
      target_link_libraries(flecs_scene PRIVATE asyncinput_shared)
      target_link_libraries(flecs_script_scene PRIVATE asyncinput_shared)
      target_link_libraries(tilemap_render PRIVATE asyncinput_shared)
      target_link_libraries(pixel_platformer PRIVATE asyncinput_shared)
      target_link_libraries(audio_pan_example PRIVATE asyncinput_shared)
      target_link_libraries(audio_ray_example PRIVATE asyncinput_shared)
  elseif(TARGET asyncinput)
      target_link_libraries(line_draw PRIVATE asyncinput)
      target_link_libraries(rhythm_game PRIVATE asyncinput)
      target_link_libraries(raymarch_arcade PRIVATE asyncinput)
      target_link_libraries(dialogue_example PRIVATE asyncinput)
      target_link_libraries(dialogue_ui_example PRIVATE asyncinput)
      target_link_libraries(flecs_scene PRIVATE asyncinput)
      target_link_libraries(flecs_script_scene PRIVATE asyncinput)
      target_link_libraries(tilemap_render PRIVATE asyncinput)
      target_link_libraries(pixel_platformer PRIVATE asyncinput)
      target_link_libraries(audio_pan_example PRIVATE asyncinput)
      target_link_libraries(audio_ray_example PRIVATE asyncinput)
  else()
      message(FATAL_ERROR "asyncinput library targets not found")
  endif()

  # Link examples against engine + deps
  if(TARGET OpenGL::GL)
      target_link_libraries(line_draw PRIVATE OpenGL::GL)
      target_link_libraries(rhythm_game PRIVATE OpenGL::GL)
      target_link_libraries(raymarch_arcade PRIVATE OpenGL::GL)
      if(TARGET text_editor)
        target_link_libraries(text_editor PRIVATE OpenGL::GL)
      endif()
      target_link_libraries(dialogue_example PRIVATE OpenGL::GL)
      target_link_libraries(dialogue_ui_example PRIVATE OpenGL::GL)
      target_link_libraries(flecs_scene PRIVATE OpenGL::GL)
      target_link_libraries(flecs_script_scene PRIVATE OpenGL::GL)
      target_link_libraries(tilemap_render PRIVATE OpenGL::GL)
      target_link_libraries(pixel_platformer PRIVATE OpenGL::GL)
      target_link_libraries(audio_pan_example PRIVATE OpenGL::GL)
      target_link_libraries(audio_opus_example PRIVATE OpenGL::GL)
      target_link_libraries(audio_ray_example PRIVATE OpenGL::GL)
  else()
      target_link_libraries(line_draw PRIVATE GL)
      target_link_libraries(rhythm_game PRIVATE GL)
      target_link_libraries(raymarch_arcade PRIVATE GL)
      if(TARGET text_editor)
        target_link_libraries(text_editor PRIVATE GL)
      endif()
      target_link_libraries(dialogue_example PRIVATE GL)
      target_link_libraries(dialogue_ui_example PRIVATE GL)
      target_link_libraries(flecs_scene PRIVATE GL)
      target_link_libraries(flecs_script_scene PRIVATE GL)
      target_link_libraries(tilemap_render PRIVATE GL)
      target_link_libraries(pixel_platformer PRIVATE GL)
      target_link_libraries(audio_pan_example PRIVATE GL)
      target_link_libraries(audio_opus_example PRIVATE GL)
      target_link_libraries(audio_ray_example PRIVATE GL)
  endif()

target_link_libraries(line_draw PRIVATE ${AME_SDL3_TARGET} Threads::Threads ame)
  target_link_libraries(rhythm_game PRIVATE ${AME_SDL3_TARGET} Threads::Threads ame)
  target_link_libraries(raymarch_arcade PRIVATE ${AME_SDL3_TARGET} Threads::Threads ame)
  target_link_libraries(dialogue_example PRIVATE ${AME_SDL3_TARGET} Threads::Threads ame)
  target_link_libraries(pixel_platformer PRIVATE ${AME_SDL3_TARGET} Threads::Threads ame ${AME_SDL3_IMAGE_TARGET})
  if(TARGET text_editor)
      target_link_libraries(text_editor PRIVATE ${AME_SDL3_TARGET} Threads::Threads ame ${AME_SDL3_TTF_TARGET})
  endif()
  target_link_libraries(dialogue_ui_example PRIVATE ${AME_SDL3_TARGET} Threads::Threads ame ${AME_SDL3_TTF_TARGET} glad)
  # Link flecs example
  if(AME_WITH_FLECS)
    target_link_libraries(flecs_scene PRIVATE ${AME_SDL3_TARGET} Threads::Threads ame flecs)
    # Link flecs script GL example
    target_link_libraries(flecs_script_scene PRIVATE ${AME_SDL3_TARGET} Threads::Threads ame flecs)
    # Link tilemap example
    target_link_libraries(tilemap_render PRIVATE ${AME_SDL3_TARGET} Threads::Threads ame flecs)
  endif()
  # Link audio example
  target_link_libraries(audio_pan_example PRIVATE ${AME_SDL3_TARGET} Threads::Threads ame)
  # Link opus example
  target_link_libraries(audio_opus_example PRIVATE ${AME_SDL3_TARGET} Threads::Threads ame)
  # Link audio ray example
  target_link_libraries(audio_ray_example PRIVATE ${AME_SDL3_TARGET} Threads::Threads ame)

  # Compile definitions for SDL3 callback main style
  add_compile_definitions(SDL_MAIN_USE_CALLBACKS=1)

  # On Linux, ensure we link with dl for GL symbol lookup if needed
  if(UNIX AND NOT APPLE)
      target_link_libraries(line_draw PRIVATE dl)
      target_link_libraries(rhythm_game PRIVATE dl)
      target_link_libraries(raymarch_arcade PRIVATE dl)
      target_link_libraries(dialogue_example PRIVATE dl)
      target_link_libraries(dialogue_ui_example PRIVATE dl)
      target_link_libraries(flecs_scene PRIVATE dl)
      target_link_libraries(flecs_script_scene PRIVATE dl)
      target_link_libraries(tilemap_render PRIVATE dl)
      target_link_libraries(pixel_platformer PRIVATE dl)
      target_link_libraries(audio_pan_example PRIVATE dl)
      target_link_libraries(audio_opus_example PRIVATE dl)
      target_link_libraries(audio_ray_example PRIVATE dl)
      # converter has no dl needs
  endif()
endif()
