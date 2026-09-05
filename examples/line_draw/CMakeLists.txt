# line_draw — freehand lines as thin quads in the one batch
add_executable(line_draw line_app.c ${AME_BOOTSTRAP_SRC})
target_link_libraries(line_draw PRIVATE ame::2d)
target_compile_definitions(line_draw PRIVATE AME_INPUT_SDL=1 AME_2D=1)
target_compile_options(line_draw PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
