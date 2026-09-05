cmake_minimum_required(VERSION 3.20)
project(ame_next C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(ASYNCINPUT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ASYNCINPUT_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ASYNCINPUT_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ASYNCINPUT_NO_SDL ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF)
add_subdirectory(external/asyncinput)
set(BUILD_TESTING ON)

find_package(SDL3 REQUIRED CONFIG)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

set(AME_WARN -Wall -Wextra -Werror -Werror=implicit-function-declaration -ffp-contract=off)

# Engine library — mongoose-style include/ame + src (no game TUs).
add_library(ame STATIC
    src/gl.c
    src/math.c
    src/camera.c
    src/gfx.c
    src/text.c
    src/input.c
    src/pool.c
    src/events.c
    src/geo.c
    src/debug.c
    src/audio.c
    src/memory.c
    src/net.c
    src/memnet.c
    src/app.c
    src/obj.c
    src/mesh.c
    src/dialogue.c
    src/time.c
    src/prefab.c
    src/tilemap.c
    src/audio_ray.c
)
target_include_directories(ame PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external/asyncinput/include
)
target_include_directories(ame PRIVATE
    ${CMAKE_SOURCE_DIR}/external/cglm/include
)
target_compile_options(ame PRIVATE ${AME_WARN})
target_link_libraries(ame PUBLIC
    asyncinput_static
    OpenGL::GL
    SDL3::SDL3
    Threads::Threads
    m
)

# Memory — mongoose examples/ layout.
add_executable(memory
    examples/memory/main.c
    examples/memory/app.c
    examples/memory/render/mem_draw.c
)
target_include_directories(memory PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/examples/memory
    ${CMAKE_SOURCE_DIR}/examples/memory/render
    ${CMAKE_SOURCE_DIR}/external/asyncinput/include
)
target_compile_definitions(memory PRIVATE AME_3D=1)
target_compile_options(memory PRIVATE ${AME_WARN})
target_link_libraries(memory PRIVATE
    ame
    SDL3::SDL3
)

# Bezier level JSON → C (and OBJ) at build time.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(LEVEL_JSON ${CMAKE_SOURCE_DIR}/assets/level/biscuit.json)
set(LEVEL_TOOL ${CMAKE_SOURCE_DIR}/tools/level_to_c.py)
set(LEVEL_GEN_DIR ${CMAKE_BINARY_DIR}/gen)
set(LEVEL_GEN_C ${LEVEL_GEN_DIR}/level_gen.c)
set(LEVEL_GEN_H ${LEVEL_GEN_DIR}/level_gen.h)
add_custom_command(
    OUTPUT ${LEVEL_GEN_C} ${LEVEL_GEN_H}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${LEVEL_GEN_DIR}
    COMMAND ${Python3_EXECUTABLE} ${LEVEL_TOOL}
            ${LEVEL_JSON}
            --c ${LEVEL_GEN_C}
            --h ${LEVEL_GEN_H}
            --obj ${LEVEL_GEN_DIR}/biscuit.obj
    DEPENDS ${LEVEL_JSON} ${LEVEL_TOOL}
    COMMENT "bezier level → C"
    VERBATIM
)

# Biscuit Fuel — brackeysjam layout (GLOB src tree like the jam CMake).
set(BF_DIR ${CMAKE_SOURCE_DIR}/examples/biscuit)
file(GLOB_RECURSE BF_SOURCES CONFIGURE_DEPENDS ${BF_DIR}/*.c)
add_executable(biscuit ${BF_SOURCES} ${LEVEL_GEN_C})
target_include_directories(biscuit PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${BF_DIR}
    ${BF_DIR}/entities
    ${BF_DIR}/render
    ${CMAKE_SOURCE_DIR}/external/asyncinput/include
    ${LEVEL_GEN_DIR}
)
target_compile_definitions(biscuit PRIVATE AME_3D=1)
target_compile_options(biscuit PRIVATE ${AME_WARN})
target_link_libraries(biscuit PRIVATE
    ame
    SDL3::SDL3
)

enable_testing()

function(ame_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(${name} PRIVATE ${AME_WARN})
    target_link_libraries(${name} PRIVATE Threads::Threads m)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

ame_test(test_pool   tests/test_pool.c   src/pool.c)
ame_test(test_events tests/test_events.c src/events.c)
ame_test(test_geo    tests/test_geo.c    src/geo.c)
ame_test(test_math   tests/test_math.c   src/math.c src/camera.c)
target_include_directories(test_math PRIVATE ${CMAKE_SOURCE_DIR}/external/cglm/include)
ame_test(test_audio  tests/test_audio.c  src/audio.c)
ame_test(test_memory tests/test_memory.c src/memory.c src/pool.c src/events.c src/geo.c)
ame_test(test_net    tests/test_net.c    src/net.c src/memnet.c src/memory.c src/pool.c src/events.c src/geo.c)
ame_test(test_coords    tests/test_coords.c)
ame_test(test_obj       tests/test_obj.c src/obj.c src/mesh.c)
ame_test(test_dialogue  tests/test_dialogue.c src/dialogue.c)
ame_test(test_batch     tests/test_batch.c src/gfx.c src/gl.c)
ame_test(test_app       tests/test_app.c src/app.c src/audio.c src/gl.c)
target_link_libraries(test_app PRIVATE SDL3::SDL3 OpenGL::GL)
ame_test(test_time      tests/test_time.c src/time.c)
ame_test(test_prefab    tests/test_prefab.c src/prefab.c)
ame_test(test_tilemap   tests/test_tilemap.c src/tilemap.c src/geo.c)
ame_test(test_audio_ray tests/test_audio_ray.c src/audio_ray.c src/geo.c)

set(BF_SIM
    ${BF_DIR}/gameplay.c
    ${BF_DIR}/abilities.c
    ${BF_DIR}/physics.c
    ${BF_DIR}/triggers.c
    ${BF_DIR}/dialogue_manager.c
    ${BF_DIR}/entities/car.c
    ${BF_DIR}/entities/human.c
)
add_executable(test_biscuit
    tests/test_biscuit.c
    ${BF_SIM}
    ${LEVEL_GEN_C}
    src/pool.c
    src/events.c
    src/geo.c
)
target_include_directories(test_biscuit PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${BF_DIR}
    ${BF_DIR}/entities
    ${LEVEL_GEN_DIR}
)
target_compile_options(test_biscuit PRIVATE ${AME_WARN})
target_link_libraries(test_biscuit PRIVATE Threads::Threads m)
add_test(NAME test_biscuit COMMAND test_biscuit)

# Regression: the example self-tests must write their BMP relative to the
# caller's working directory (they used to hard-code /home/user/ame-next/...).
add_test(NAME test_selftest_paths
         COMMAND ${CMAKE_COMMAND}
                 -DMEMORY_EXE=$<TARGET_FILE:memory>
                 -DBISCUIT_EXE=$<TARGET_FILE:biscuit>
                 -P ${CMAKE_SOURCE_DIR}/cmake/run_selftest_paths.cmake)

# Reference C emitted from the Lean model (generated/ame_gen.c).
add_executable(test_gen tests/test_gen.c generated/ame_gen.c)
target_include_directories(test_gen PRIVATE ${CMAKE_SOURCE_DIR}/generated)
target_compile_options(test_gen PRIVATE ${AME_WARN})
add_test(NAME test_gen COMMAND test_gen)
