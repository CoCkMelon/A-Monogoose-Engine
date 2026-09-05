# memory_game — the FIRST GAME vertical slice (README FIRST GAME, Stage 0):
# local hot-seat Memory with real 3D flipping card panels + billboard UI.
# Each game/example owns its CMakeLists and links the engine libs (like
# A-Monogoose examples did).

# default build: SDL input backend (works everywhere, incl. CI)
add_executable(memory_game
    mem_sim.c
    mem_net.c
    mem_app.c
    ${AME_BOOTSTRAP_SRC})
target_link_libraries(memory_game PRIVATE ame::3d ame_font)
target_compile_definitions(memory_game PRIVATE AME_INPUT_SDL=1)
target_include_directories(memory_game PRIVATE ${CMAKE_SOURCE_DIR}/external/stb)
target_compile_options(memory_game PRIVATE -Wall -Wextra -Werror -ffp-contract=off)

# Stage 1: authoritative server — pure POSIX + the pure sim (no SDL/GL)
add_executable(mem_server
    mem_sim.c
    mem_net.c
    mem_server.c)
target_include_directories(mem_server PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(mem_server PRIVATE AME_3D=1)
target_compile_options(mem_server PRIVATE -Wall -Wextra -Werror -ffp-contract=off)

# deploy.txt fallback: if SDL3 ended up DYNAMIC in a release build,
# tools/pack.sh bundles the needed libs into lib/ BESIDE this binary;
# the $ORIGIN rpath makes the loader find them there (never the old
# "copied beside but not found" trap).
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_options(memory_game PRIVATE "-Wl,-rpath,$ORIGIN/lib")
endif()

# variant: raw asyncinput backend (needs /dev/input read access at runtime)
if(AME_BUILD_ASYNCINPUT_GAME)
    add_executable(memory_game_asyncinput
        mem_sim.c
        mem_net.c
        mem_app.c
        ${CMAKE_SOURCE_DIR}/src/input_asyncinput.c
        ${AME_BOOTSTRAP_SRC})
    target_link_libraries(memory_game_asyncinput PRIVATE
        ame::3d ame_font asyncinput_static)
    # screenshot path uses stb_image_write, same as the SDL variant;
    # the asyncinput backend needs the vendored header
    target_include_directories(memory_game_asyncinput PRIVATE
        ${CMAKE_SOURCE_DIR}/external/stb
        ${CMAKE_SOURCE_DIR}/external/asyncinput/include)
    target_compile_definitions(memory_game_asyncinput PRIVATE AME_INPUT_ASYNCINPUT=1)
    target_compile_options(memory_game_asyncinput PRIVATE
        -Wall -Wextra -Werror -ffp-contract=off)
endif()
