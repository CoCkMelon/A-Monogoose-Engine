cmake_minimum_required(VERSION 3.24)
project(my_ame_game LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(ame CONFIG REQUIRED)

add_executable(my_ame_game main.c)
target_link_libraries(my_ame_game PRIVATE ame::2d ame::bootstrap2d)
target_compile_options(my_ame_game PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
