# memory_game — the FIRST GAME vertical slice (README FIRST GAME, Stage 0):
# local hot-seat Memory with real 3D flipping card panels + billboard UI.
# Each game/example owns its CMakeLists and links the engine libs (like
# A-Monogoose examples did).

# default build: SDL input backend (works everywhere, incl. CI)
add_executable(memory_game
    mem_sim.c
    mem_app.c
    ${AME_BOOTSTRAP_SRC})
target_link_libraries(memory_game PRIVATE ame::3d ame_font)
target_compile_definitions(memory_game PRIVATE AME_INPUT_SDL=1)
target_include_directories(memory_game PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)
target_compile_options(memory_game PRIVATE -Wall -Wextra -Werror -ffp-contract=off)

# variant: raw asyncinput backend (needs /dev/input read access at runtime)
if(AME_BUILD_ASYNCINPUT_GAME)
    add_executable(memory_game_asyncinput
        mem_sim.c
        mem_app.c
        ${AME_BOOTSTRAP_SRC})
    target_link_libraries(memory_game_asyncinput PRIVATE
        ame::3d ame_font asyncinput_static)
    target_compile_definitions(memory_game_asyncinput PRIVATE AME_INPUT_ASYNCINPUT=1)
    target_compile_options(memory_game_asyncinput PRIVATE
        -Wall -Wextra -Werror -ffp-contract=off)
endif()
