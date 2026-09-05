# tests — each suite is a small standalone binary; logic tests run headless
# on every CI job; render/GL tests run under headless GL where available.
# Tests build in BOTH dimensions where dimension matters (build.txt).

# dimension-agnostic suites
add_executable(test_math   test_math.c)
add_executable(test_pool   test_pool.c)
target_link_libraries(test_math PRIVATE ame)
target_link_libraries(test_pool PRIVATE ame)

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
add_test(NAME pool   COMMAND test_pool)
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

# text layout (pure CPU; glyph metrics from the baked atlas)
add_executable(test_text test_text.c)
target_link_libraries(test_text PRIVATE ame::3d)
add_test(NAME text COMMAND test_text)

# golden render: headless EGL surfaceless + llvmpipe (Linux CI job)
if(UNIX AND NOT APPLE)
    add_executable(test_render test_render.c)
    target_link_libraries(test_render PRIVATE ame::3d ame_font EGL)
    target_include_directories(test_render PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)
    target_compile_definitions(test_render PRIVATE SDL_MAIN_HANDLED=1)
    target_compile_options(test_render PRIVATE -Wno-error=implicit-function-declaration)
    add_test(NAME render COMMAND test_render)
    set_tests_properties(render PROPERTIES
        ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
endif()
