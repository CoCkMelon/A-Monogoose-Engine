# ame-next — one C23 core, 2D+3D, many targets (see docs/spec in the repo wiki
# and docs folded into each module header). Build per build.txt.
cmake_minimum_required(VERSION 3.24)

project(ame_next
    VERSION 0.1.0
    DESCRIPTION "ame-next game engine (C23 core, 2D+3D, SDL3+GL)"
    LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "build type" FORCE)
endif()

option(AME_BUILD_EXAMPLES "Build examples" ON)
option(AME_BUILD_TESTS    "Build tests"    ON)
option(AME_ENABLE_ASAN    "ASan/UBSan debug build" OFF)
option(AME_BUILD_ASYNCINPUT_GAME "Build the asyncinput-backend game variant" OFF)

# ---------------------------------------------------------------------------
# FP behaviour is PINNED for determinism (principles.txt): no fast-math,
# no FP contraction, on every target.
# ---------------------------------------------------------------------------
set(AME_FP_FLAGS "-ffp-contract=off")

# The engine's OWN code is warnings-as-errors. third_party/ and tools/ are
# exempt (build.txt rule 3 / vendored-code exemption).
set(AME_WARN_FLAGS "-Wall;-Wextra;-Werror")

# ---------------------------------------------------------------------------
# Generated content: the font atlas is baked at BUILD TIME (text.txt) by a
# host tool from assets/fonts/PixelifySans-Regular.ttf. tools/ comes before
# the engine libs so ame_font exists for them to link.
# ---------------------------------------------------------------------------
add_subdirectory(tools)

# ---------------------------------------------------------------------------
# Dimension is a BUILD option (build.txt rule 6): the engine lib is
# instantiated once per dimension from the SAME sources. AME_DIM selects the
# default; tests/examples/games link ame_2d or ame_3d explicitly.
# ---------------------------------------------------------------------------
set(AME_DIM "3" CACHE STRING "Default engine dimension for the 'ame' alias: 2 or 3")
if(NOT AME_DIM MATCHES "^[23]$")
    message(FATAL_ERROR "AME_DIM must be 2 or 3")
endif()

file(GLOB AME_ENGINE_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
# the SDL bootstrap is THIN PER-TARGET glue (input backend is a target
# choice, build.txt rule 7): games compile it themselves via AME_BOOTSTRAP_SRC
list(FILTER AME_ENGINE_SRC EXCLUDE REGEX ".*/app_sdl\.c$")
set(AME_BOOTSTRAP_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/app_sdl.c")

function(ame_dim_lib dim)
    add_library(ame_${dim}d STATIC ${AME_ENGINE_SRC})
    target_include_directories(ame_${dim}d PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
    target_compile_options(ame_${dim}d PRIVATE ${AME_WARN_FLAGS} ${AME_FP_FLAGS})
    target_compile_definitions(ame_${dim}d PUBLIC AME_${dim}D=1)
    target_link_libraries(ame_${dim}d PUBLIC ${AME_SDL3_LINK})
    target_link_libraries(ame_${dim}d PRIVATE ame_font)
    if(UNIX AND NOT APPLE)
        target_link_libraries(ame_${dim}d PUBLIC m)
    endif()
    if(AME_ENABLE_ASAN)
        target_compile_options(ame_${dim}d PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(ame_${dim}d PUBLIC -fsanitize=address,undefined)
    endif()
    add_library(ame::${dim}d ALIAS ame_${dim}d)
endfunction()

find_package(SDL3 REQUIRED)

# deploy.txt LIBRARY LINKING: release links SDL3 STATICALLY wherever
# available (the "works on my machine, missing .so on a clean one" bug
# class). Non-release builds keep the shared lib (dev ergonomics). If a
# static SDL3 is unavailable in Release, tools/pack.sh falls back to
# bundling the dynamic libs beside the game (found via $ORIGIN rpath,
# set in examples/memory_game/CMakeLists.txt).
if(NOT DEFINED AME_SDL3_STATIC)
    if(CMAKE_BUILD_TYPE STREQUAL "Release" AND TARGET SDL3::SDL3-static)
        set(AME_SDL3_STATIC ON)
    else()
        set(AME_SDL3_STATIC OFF)
    endif()
endif()
if(AME_SDL3_STATIC AND TARGET SDL3::SDL3-static)
    message(STATUS "ame: linking SDL3 STATICALLY (deploy.txt release rule)")
    set(AME_SDL3_LINK SDL3::SDL3-static)
else()
    set(AME_SDL3_LINK SDL3::SDL3)
endif()

# asyncinput: bundled dep of AME_INPUT_ASYNCINPUT builds (input.txt/build.txt)
set(ASYNCINPUT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ASYNCINPUT_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(AME_SAVE_BUILD_TESTING ${BUILD_TESTING})
set(BUILD_TESTING OFF) # asyncinput's own smoke test stays out of our ctest
add_subdirectory(third_party/asyncinput EXCLUDE_FROM_ALL)
set(BUILD_TESTING ${AME_SAVE_BUILD_TESTING})

ame_dim_lib(2)
ame_dim_lib(3)
add_library(ame ALIAS ame_${AME_DIM}d)

# ---------------------------------------------------------------------------
# Helper: declare an engine-consuming target with dimension + input backend.
#   ame_add_target(<name> DIM 2|3 INPUT sdl|asyncinput [HEADLESS] ...)
# ---------------------------------------------------------------------------
function(ame_add_target name)
    set(options HEADLESS)
    set(oneValue DIM INPUT)
    set(multiValue SOURCES)
    cmake_parse_arguments(AMET "${options}" "${oneValue}" "${multiValue}" ${ARGN})

    add_executable(${name} ${AMET_SOURCES})
    target_link_libraries(${name} PRIVATE ame::${AMET_DIM}d)

    if(AMET_DIM STREQUAL "2")
        target_compile_definitions(${name} PRIVATE AME_2D=1)
    elseif(AMET_DIM STREQUAL "3")
        target_compile_definitions(${name} PRIVATE AME_3D=1)
    else()
        message(FATAL_ERROR "ame_add_target(${name}): DIM must be 2 or 3")
    endif()

    if(AMET_INPUT STREQUAL "sdl")
        target_compile_definitions(${name} PRIVATE AME_INPUT_SDL=1)
    elseif(AMET_INPUT STREQUAL "asyncinput")
        target_compile_definitions(${name} PRIVATE AME_INPUT_ASYNCINPUT=1)
    elseif(NOT AMET_INPUT STREQUAL "")
        message(FATAL_ERROR "ame_add_target(${name}): INPUT must be sdl or asyncinput")
    endif()

    if(AMET_HEADLESS)
        target_compile_definitions(${name} PRIVATE AME_HEADLESS=1)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Tests (build.txt: every job builds tests in BOTH dimensions and RUNS them).
# ---------------------------------------------------------------------------
if(AME_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(AME_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

