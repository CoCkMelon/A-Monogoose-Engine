# raymarch_arcade — the master's SDF game as a CPU shader into ONE
# dynamic texture + one batch quad (A-Monogoose port)
add_executable(raymarch_arcade rm_app.c ${AME_BOOTSTRAP_SRC})
target_link_libraries(raymarch_arcade PRIVATE ame::2d m)
target_compile_definitions(raymarch_arcade PRIVATE AME_INPUT_SDL=1 AME_2D=1)
target_compile_options(raymarch_arcade PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
