cmake_minimum_required(VERSION 3.16)
project(a_mongoose_engine C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find SDL3 and OpenGL
find_package(SDL3 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(SDL3_ttf REQUIRED)

# Vendored glad
add_library(glad STATIC third_party/glad/src/gl.c)
target_include_directories(glad PUBLIC third_party/glad/include)

# Flecs ECS
add_subdirectory(third_party/flecs)

# Bring in the sibling asyncinput library for examples
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../asyncinput ${CMAKE_CURRENT_BINARY_DIR}/asyncinput_build)

# Public headers
include_directories(
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/../asyncinput/include
    ${CMAKE_CURRENT_LIST_DIR}/src
)

# Engine objects (initial modules)
add_library(ame STATIC
    src/camera.c
    src/gl_loader.c
    src/dialogue.c
    src/embedded_dialogues.c
    src/story_route.c
    src/ecs.c
    src/tilemap.c
    src/audio.c
)

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

target_link_libraries(ame PUBLIC SDL3::SDL3)

# Link vendored glad
target_link_libraries(ame PUBLIC glad flecs)

# Audio dependencies via pkg-config
pkg_check_modules(PORTAUDIO REQUIRED IMPORTED_TARGET portaudio-2.0)
pkg_check_modules(OPUSFILE REQUIRED IMPORTED_TARGET opusfile)

target_link_libraries(ame PUBLIC PkgConfig::PORTAUDIO PkgConfig::OPUSFILE)

# Link math library where needed (Linux)
if(UNIX AND NOT APPLE)
    target_link_libraries(ame PUBLIC m)
endif()

target_include_directories(ame PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

# Ensure story route header is public
install(FILES include/ame_story_route.h DESTINATION include)

# Example: line_draw
add_executable(line_draw
    examples/line_draw/main.c
)

# Example: rhythm_game
add_executable(rhythm_game
    examples/rhythm_game/main.c
)

# Example: raymarch_arcade
add_executable(raymarch_arcade
    examples/raymarch_arcade/main.c
)

# Example: text_editor (requires SDL3_ttf)
option(AME_TEXT_EDITOR_USE_ASYNCINPUT "Build text_editor using asyncinput backend (defines AME_USE_ASYNCINPUT)" OFF)
add_executable(text_editor
    examples/text_editor/main.c
)
if(AME_TEXT_EDITOR_USE_ASYNCINPUT)
    target_compile_definitions(text_editor PRIVATE AME_USE_ASYNCINPUT=1)
endif()
# Link ttf and glad already via ame

# Example: dialogue_example (console-style)
add_executable(dialogue_example
    examples/dialogue_example/main.c
)

# Example: dialogue_ui_example (OpenGL + SDL3_ttf)
add_executable(dialogue_ui_example
    examples/dialogue_ui_example/main.c
)

# Example: flecs_scene (interactive ECS scene)
add_executable(flecs_scene
    examples/flecs_scene/main.c
)

# Example: tilemap_render (Tiled JSON tilemap rendering)
add_executable(tilemap_render
    examples/tilemap_render/main.c
)

# Example: audio panning with ECS-controlled source
add_executable(audio_pan_example
    examples/audio_pan_example/main.c
)

# Example: audio opus playback from file
add_executable(audio_opus_example
    examples/audio_opus_example/main.c
)

# Prefer the shared lib if enabled, fall back to static otherwise
if(TARGET asyncinput_shared)
    target_link_libraries(line_draw PRIVATE asyncinput_shared)
    target_link_libraries(rhythm_game PRIVATE asyncinput_shared)
    target_link_libraries(raymarch_arcade PRIVATE asyncinput_shared)
    target_link_libraries(dialogue_example PRIVATE asyncinput_shared)
    target_link_libraries(dialogue_ui_example PRIVATE asyncinput_shared)
elseif(TARGET asyncinput_static)
    target_link_libraries(line_draw PRIVATE asyncinput_static)
    target_link_libraries(rhythm_game PRIVATE asyncinput_static)
    target_link_libraries(raymarch_arcade PRIVATE asyncinput_static)
    target_link_libraries(dialogue_example PRIVATE asyncinput_static)
    target_link_libraries(dialogue_ui_example PRIVATE asyncinput_static)
    target_link_libraries(flecs_scene PRIVATE asyncinput_static)
    target_link_libraries(tilemap_render PRIVATE asyncinput_static)
    target_link_libraries(audio_pan_example PRIVATE asyncinput_static)
else()
    message(FATAL_ERROR "asyncinput library targets not found")
endif()

# Link example against engine + deps
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
    target_link_libraries(tilemap_render PRIVATE OpenGL::GL)
    target_link_libraries(audio_pan_example PRIVATE OpenGL::GL)
    target_link_libraries(audio_opus_example PRIVATE OpenGL::GL)
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
    target_link_libraries(tilemap_render PRIVATE GL)
    target_link_libraries(audio_pan_example PRIVATE GL)
    target_link_libraries(audio_opus_example PRIVATE GL)
endif()

target_link_libraries(line_draw PRIVATE SDL3::SDL3 Threads::Threads ame)
target_link_libraries(rhythm_game PRIVATE SDL3::SDL3 Threads::Threads ame)
 target_link_libraries(raymarch_arcade PRIVATE SDL3::SDL3 Threads::Threads ame)
 target_link_libraries(dialogue_example PRIVATE SDL3::SDL3 Threads::Threads ame)
 if(TARGET text_editor)
    target_link_libraries(text_editor PRIVATE SDL3::SDL3 Threads::Threads ame SDL3_ttf::SDL3_ttf)
    if(AME_TEXT_EDITOR_USE_ASYNCINPUT)
        if(TARGET asyncinput_shared)
            target_link_libraries(text_editor PRIVATE asyncinput_shared)
        elseif(TARGET asyncinput_static)
            target_link_libraries(text_editor PRIVATE asyncinput_static)
        endif()
    endif()
 endif()
target_link_libraries(dialogue_ui_example PRIVATE SDL3::SDL3 Threads::Threads ame SDL3_ttf::SDL3_ttf glad)
# Link flecs example
target_link_libraries(flecs_scene PRIVATE SDL3::SDL3 Threads::Threads ame flecs)
# Link tilemap example
target_link_libraries(tilemap_render PRIVATE SDL3::SDL3 Threads::Threads ame flecs)
# Link audio example
target_link_libraries(audio_pan_example PRIVATE SDL3::SDL3 Threads::Threads ame)
# Link opus example
target_link_libraries(audio_opus_example PRIVATE SDL3::SDL3 Threads::Threads ame)

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
    target_link_libraries(tilemap_render PRIVATE dl)
    target_link_libraries(audio_pan_example PRIVATE dl)
    target_link_libraries(audio_opus_example PRIVATE dl)
endif()
