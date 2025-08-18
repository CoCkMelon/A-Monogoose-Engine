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
)

# SDL/OpenGL for engine (GL loader needs it)
if(TARGET OpenGL::GL)
    target_link_libraries(ame PUBLIC OpenGL::GL)
else()
    target_link_libraries(ame PUBLIC GL)
endif()

target_link_libraries(ame PUBLIC SDL3::SDL3)

# Link math library where needed (Linux)
if(UNIX AND NOT APPLE)
    target_link_libraries(ame PUBLIC m)
endif()

target_include_directories(ame PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

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
#if(SDL3_TTF_FOUND)
    add_executable(text_editor
        examples/text_editor/main.c
    )

    if(TARGET OpenGL::GL)
        target_link_libraries(text_editor PRIVATE OpenGL::GL)
    else()
        target_link_libraries(text_editor PRIVATE GL)
    endif()

    target_link_libraries(text_editor PRIVATE SDL3::SDL3 Threads::Threads ame SDL3_ttf::SDL3_ttf)

    if(UNIX AND NOT APPLE)
        target_link_libraries(text_editor PRIVATE dl)
    endif()
    #endif()

# Prefer the shared lib if enabled, fall back to static otherwise
if(TARGET asyncinput_shared)
    target_link_libraries(line_draw PRIVATE asyncinput_shared)
    target_link_libraries(rhythm_game PRIVATE asyncinput_shared)
    target_link_libraries(raymarch_arcade PRIVATE asyncinput_shared)
    if(TARGET text_editor)
        target_link_libraries(text_editor PRIVATE asyncinput_shared)
    endif()
elseif(TARGET asyncinput_static)
    target_link_libraries(line_draw PRIVATE asyncinput_static)
    target_link_libraries(rhythm_game PRIVATE asyncinput_static)
    target_link_libraries(raymarch_arcade PRIVATE asyncinput_static)
    if(TARGET text_editor)
        target_link_libraries(text_editor PRIVATE asyncinput_static)
    endif()
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
else()
    target_link_libraries(line_draw PRIVATE GL)
    target_link_libraries(rhythm_game PRIVATE GL)
    target_link_libraries(raymarch_arcade PRIVATE GL)
    if(TARGET text_editor)
        target_link_libraries(text_editor PRIVATE GL)
    endif()
endif()

target_link_libraries(line_draw PRIVATE SDL3::SDL3 Threads::Threads ame)
target_link_libraries(rhythm_game PRIVATE SDL3::SDL3 Threads::Threads ame)
target_link_libraries(raymarch_arcade PRIVATE SDL3::SDL3 Threads::Threads ame)
if(TARGET text_editor)
    target_link_libraries(text_editor PRIVATE SDL3::SDL3 Threads::Threads ame)
endif()

# Compile definitions for SDL3 callback main style
add_compile_definitions(SDL_MAIN_USE_CALLBACKS=1)

# On Linux, ensure we link with dl for GL symbol lookup if needed
if(UNIX AND NOT APPLE)
    target_link_libraries(line_draw PRIVATE dl)
    target_link_libraries(rhythm_game PRIVATE dl)
    target_link_libraries(raymarch_arcade PRIVATE dl)
    if(TARGET text_editor)
        target_link_libraries(text_editor PRIVATE dl)
    endif()
endif()
