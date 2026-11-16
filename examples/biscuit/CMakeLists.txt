# Biscuit Fuel — the game-lineage vertical slice ported onto trunk:
# app_* hooks (loop.txt), trunk renderer (rp_push_*), trunk text HUD,
# trunk pools/prefab, and the dual geometry path (runtime OBJ in dev,
# baked level_gen arrays as the cook fallback). Folder split kept as the
# template for games: gameplay/ sim vs render/ vs entities/.

find_package(Threads REQUIRED)
ame_add_app(biscuit
    abilities.c
    app.c
    bf_audio.c
    dialogue_manager.c
    gameplay.c
    input.c
    level_gen.c
    phys_geo.c
    physics.c
    triggers.c
    ui.c
    entities/car.c
    entities/human.c
    render/pipeline.c
    ${AME_BOOTSTRAP_SRC})
target_include_directories(biscuit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/render)
target_link_libraries(biscuit PRIVATE ame::2d ame_font Threads::Threads)
target_compile_definitions(biscuit PRIVATE AME_INPUT_SDL=1)
# dev path: the level OBJ straight from the source tree, so editing the
# export + hot reload needs no recompile and no rebake (levels.txt)
target_compile_definitions(biscuit PRIVATE
    BF_LEVEL_OBJ="${CMAKE_SOURCE_DIR}/assets/level/biscuit.obj")
target_compile_options(biscuit PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
