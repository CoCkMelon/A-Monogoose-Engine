# tools — BUILD-TIME host tools (never linked into the engine).
# The font baker is ours but includes vendored stb_truetype, so it compiles
# WITHOUT warnings-as-errors (vendored-code exemption, build.txt rule 3).

add_executable(bake_font bake_font.c)
target_include_directories(bake_font PRIVATE ${CMAKE_SOURCE_DIR}/external/stb)
target_compile_options(bake_font PRIVATE -Wall -Wextra -ffp-contract=off)
if(UNIX AND NOT APPLE)
    target_link_libraries(bake_font PRIVATE m)
endif()
set_target_properties(bake_font PROPERTIES C_STANDARD 23)

# generate the atlas at build time; regenerate when font/tool change
set(AME_FONT_PX 32 CACHE STRING "baked font pixel size")
add_custom_command(
    OUTPUT  ${CMAKE_SOURCE_DIR}/generated/font_atlas.c
            ${CMAKE_SOURCE_DIR}/generated/font_atlas.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/generated
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_SOURCE_DIR}/generated
            $<TARGET_FILE:bake_font> ${CMAKE_SOURCE_DIR}/assets/fonts/PixelifySans-Regular.ttf
            ${AME_FONT_PX} font_atlas
    DEPENDS bake_font ${CMAKE_SOURCE_DIR}/assets/fonts/PixelifySans-Regular.ttf
    COMMENT "baking font atlas (${AME_FONT_PX}px)"
    VERBATIM)
add_custom_target(generated_font DEPENDS ${CMAKE_SOURCE_DIR}/generated/font_atlas.c)

# DSDF baker (dense first-order distance fields; Acta Cybernetica 25,
# 2021). bakes DejaVuSans -> RGBA atlas (R=dist GB=gradient) + metrics.
add_executable(bake_font_dsdf bake_font_dsdf.c)
target_include_directories(bake_font_dsdf PRIVATE ${CMAKE_SOURCE_DIR}/external/stb)
target_compile_options(bake_font_dsdf PRIVATE -Wall -Wextra -ffp-contract=off)
if(UNIX AND NOT APPLE)
    target_link_libraries(bake_font_dsdf PRIVATE m)
endif()
set_target_properties(bake_font_dsdf PROPERTIES C_STANDARD 23)
add_custom_command(
    OUTPUT  ${CMAKE_SOURCE_DIR}/generated/font_atlas_dsdf.c
            ${CMAKE_SOURCE_DIR}/generated/font_atlas_dsdf.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/generated
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_SOURCE_DIR}/generated
            $<TARGET_FILE:bake_font_dsdf> ${CMAKE_SOURCE_DIR}/assets/fonts/DejaVuSans.ttf
            font_atlas_dsdf
    DEPENDS bake_font_dsdf ${CMAKE_SOURCE_DIR}/assets/fonts/DejaVuSans.ttf
    COMMENT "baking DSDF font atlas (DejaVuSans)"
    VERBATIM)
add_custom_target(generated_font_dsdf
    DEPENDS ${CMAKE_SOURCE_DIR}/generated/font_atlas_dsdf.c)

# static lib the engine/game links to get the baked arrays
add_library(ame_font STATIC ${CMAKE_SOURCE_DIR}/generated/font_atlas.c)
target_include_directories(ame_font PUBLIC ${CMAKE_SOURCE_DIR}/generated)
add_dependencies(ame_font generated_font)
target_sources(ame_font PRIVATE ${CMAKE_SOURCE_DIR}/generated/font_atlas_dsdf.c)
add_dependencies(ame_font generated_font_dsdf)
target_compile_options(ame_font PRIVATE -w) # generated data: silence

# assimp2c: build-time model baker (levels.txt Assimp rule). Optional -
# baked outputs are COMMITTED (tests/assets/*.h), so the engine/tests
# build fine without Assimp; install libassimp-dev only to re-bake.
find_package(assimp QUIET)
if(assimp_FOUND)
    add_executable(assimp2c assimp2c.c)
    target_link_libraries(assimp2c PRIVATE assimp::assimp)
    message(STATUS "ame: assimp2c tool available (assimp ${assimp_VERSION})")
else()
    message(STATUS "ame: assimp not found - assimp2c tool skipped "
                   "(baked assets are committed)")
endif()
# opus asset baker: deterministic Ogg Opus test asset (libopus encoder
# + hand-muxed container). Output feeds tests/test_audio_opus.c.
if(PkgConfig_FOUND AND OPUS_FOUND)
    add_executable(mkopus_sine mkopus_sine.c)
    target_link_libraries(mkopus_sine PRIVATE ${OPUS_LIBRARIES} m)
    target_include_directories(mkopus_sine PRIVATE ${OPUS_INCLUDE_DIRS})
    add_custom_command(
        OUTPUT ${CMAKE_SOURCE_DIR}/tests/assets/baked_sine_opus.h
        COMMAND mkopus_sine ${CMAKE_SOURCE_DIR}/tests/assets/baked_sine_opus.h
        DEPENDS mkopus_sine mkopus_sine.c
        COMMENT "baking tests/assets/baked_sine_opus.h")
    add_custom_target(bake_opus_asset
        DEPENDS ${CMAKE_SOURCE_DIR}/tests/assets/baked_sine_opus.h)
endif()
