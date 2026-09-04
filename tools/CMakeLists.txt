# tools — BUILD-TIME host tools (never linked into the engine).
# The font baker is ours but includes vendored stb_truetype, so it compiles
# WITHOUT warnings-as-errors (vendored-code exemption, build.txt rule 3).

add_executable(bake_font bake_font.c)
target_include_directories(bake_font PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)
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

# static lib the engine/game links to get the baked arrays
add_library(ame_font STATIC ${CMAKE_SOURCE_DIR}/generated/font_atlas.c)
target_include_directories(ame_font PUBLIC ${CMAKE_SOURCE_DIR}/generated)
add_dependencies(ame_font generated_font)
target_compile_options(ame_font PRIVATE -w) # generated data: silence
