# text_editor — editing buffer + caret + selection through the ONE
# batch (A-Monogoose port; baked font atlas instead of SDL_ttf)
add_executable(text_editor ed_app.c ${AME_BOOTSTRAP_SRC})
target_link_libraries(text_editor PRIVATE ame::2d ame_font)
target_compile_definitions(text_editor PRIVATE AME_INPUT_SDL=1 AME_2D=1)
target_compile_options(text_editor PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
