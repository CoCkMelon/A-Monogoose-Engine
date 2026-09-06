# tests — each suite is a small standalone binary; logic tests run headless
# on every CI job; render/GL tests run under headless GL where available.
# Tests build in BOTH dimensions where dimension matters (build.txt).

# dimension-agnostic suites
add_executable(test_math   test_math.c)
# snapshot seqlock: real threads (TSan target) - needs pthreads
find_package(Threads REQUIRED)
add_executable(test_snap   test_snap.c)
target_link_libraries(test_snap PRIVATE ame Threads::Threads)
add_executable(test_pool   test_pool.c)
add_executable(test_particles test_particles.c)
add_executable(test_audio_ray test_audio_ray.c)
target_link_libraries(test_math PRIVATE ame)
target_link_libraries(test_pool PRIVATE ame)
target_link_libraries(test_particles PRIVATE ame)
target_link_libraries(test_audio_ray PRIVATE ame::2d)
target_compile_definitions(test_audio_ray PRIVATE AME_2D=1)

add_executable(test_camera test_camera.c)
target_link_libraries(test_camera PRIVATE ame::3d)
target_compile_definitions(test_camera PRIVATE SDL_MAIN_HANDLED=1)
add_test(NAME camera COMMAND test_camera)

# dimension-specific suites: built once per dimension
foreach(dim 2 3)
    add_executable(test_geometry_${dim}d test_geometry.c)
    target_link_libraries(test_geometry_${dim}d PRIVATE ame::${dim}d)
    target_compile_definitions(test_geometry_${dim}d PRIVATE AME_${dim}D=1)
endforeach()

add_test(NAME math   COMMAND test_math)
add_test(NAME snap   COMMAND test_snap)
add_test(NAME pool   COMMAND test_pool)
add_test(NAME particles COMMAND test_particles)
add_test(NAME audio_ray COMMAND test_audio_ray)
add_test(NAME geo2d  COMMAND test_geometry_2d)
add_test(NAME geo3d  COMMAND test_geometry_3d)

add_executable(test_input test_input.c)
target_link_libraries(test_input PRIVATE ame::3d)
add_executable(test_audio test_audio.c)
target_link_libraries(test_audio PRIVATE ame::3d)
add_test(NAME input COMMAND test_input)
add_test(NAME audio COMMAND test_audio)

# memory game sim (deterministic golden replay)
add_executable(test_mem_sim test_mem_sim.c
    ${CMAKE_SOURCE_DIR}/examples/memory_game/mem_sim.c)
target_link_libraries(test_mem_sim PRIVATE ame::3d)
target_include_directories(test_mem_sim PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/memory_game)
add_test(NAME mem_sim COMMAND test_mem_sim)

# Stage 1 exit: authoritative online Memory over loopback — two client
# sessions play a REAL server process ($<TARGET_FILE:mem_server>) to a
# verified winner, one client drops mid-game, a rejoining client gets a
# mid-game STATE snapshot and the game resumes to completion.
add_executable(test_mem_net test_mem_net.c
    ${CMAKE_SOURCE_DIR}/examples/memory_game/mem_sim.c
    ${CMAKE_SOURCE_DIR}/examples/memory_game/mem_net.c)
target_include_directories(test_mem_net PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/memory_game)
target_link_libraries(test_mem_net PRIVATE ame::3d)
target_compile_options(test_mem_net PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
add_test(NAME mem_net COMMAND test_mem_net $<TARGET_FILE:mem_server>)
set_tests_properties(mem_net PROPERTIES TIMEOUT 180)

# text layout (pure CPU; glyph metrics from the baked atlas)
add_executable(test_text test_text.c)
target_link_libraries(test_text PRIVATE ame::3d ame_font)
add_test(NAME text COMMAND test_text)

# golden render: headless EGL surfaceless + llvmpipe (Linux CI job)
if(UNIX AND NOT APPLE)
    add_executable(test_render test_render.c)
    target_link_libraries(test_render PRIVATE ame::3d ame_font EGL)
    add_executable(test_render_2d test_render_2d.c)
    target_link_libraries(test_render_2d PRIVATE ame::2d EGL)
    target_compile_definitions(test_render_2d PRIVATE AME_2D=1 SDL_MAIN_HANDLED=1)
    target_compile_options(test_render_2d PRIVATE -Wno-error=implicit-function-declaration)
    add_test(NAME render_2d COMMAND test_render_2d)

# opus decode test: needs opusfile (engine feature) + the asset baked
# by tools/mkopus_sine (needs the opus ENCODER). Same optional-dep
# pattern as the cglm oracle below.
if(OPUSFILE_FOUND AND OPUS_FOUND)
    add_executable(test_audio_opus test_audio_opus.c)
    target_link_libraries(test_audio_opus PRIVATE ame::3d)
    target_include_directories(test_audio_opus PRIVATE
        ${CMAKE_SOURCE_DIR}/tests/assets)
    add_test(NAME audio_opus COMMAND test_audio_opus)
    add_dependencies(test_audio_opus bake_opus_asset)
else()
    message(STATUS "tests: opus test skipped (needs libopusfile-dev + libopus-dev)")
endif()

# math parity vs cglm (oracle + perf table; cglm is a TEST-ONLY dep)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(CGLM QUIET cglm)
endif()
if(CGLM_FOUND)
    add_executable(test_math_cglm test_math_cglm.c)
    target_link_libraries(test_math_cglm PRIVATE ame::3d ${CGLM_LIBRARIES})
    target_include_directories(test_math_cglm PRIVATE ${CGLM_INCLUDE_DIRS})
    target_compile_options(test_math_cglm PRIVATE -ffp-contract=off)
    add_test(NAME math_cglm COMMAND test_math_cglm)
else()
    message(STATUS "ame: cglm not found - math parity oracle test skipped")
endif()

# dialogue (needs libfyaml - conditional like the engine module)
if(FYAML_FOUND)
    add_executable(test_dialogue test_dialogue.c)
    target_link_libraries(test_dialogue PRIVATE ame::3d ${FYAML_LIBRARIES})
    target_include_directories(test_dialogue PRIVATE ${FYAML_INCLUDE_DIRS})
    target_compile_definitions(test_dialogue PRIVATE
        SDL_MAIN_HANDLED=1 CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME dialogue COMMAND test_dialogue)
endif()
    target_include_directories(test_render PRIVATE ${CMAKE_SOURCE_DIR}/external/stb)
    target_compile_definitions(test_render PRIVATE SDL_MAIN_HANDLED=1)
    target_compile_options(test_render PRIVATE -Wno-error=implicit-function-declaration)
    add_test(NAME render COMMAND test_render)
    set_tests_properties(render PROPERTIES
        ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
endif()

# editor caret contract - compiled from the REAL app source (not a
# replica): two-ways law (drawn caret == pen where a new symbol
# lands; caret left of new ink), determinism, midpoint hit-tests.
add_executable(test_editor_geom
    ${CMAKE_SOURCE_DIR}/examples/text_editor/ed_app.c)
target_compile_definitions(test_editor_geom PRIVATE
    AME_2D=1 AME_INPUT_SDL=1 AME_ED_SELFTEST=1)
target_link_libraries(test_editor_geom PRIVATE ame::2d ame_font)
target_compile_options(test_editor_geom PRIVATE
    -Wall -Wextra -Werror -ffp-contract=off)
add_test(NAME editor_geom COMMAND test_editor_geom)
