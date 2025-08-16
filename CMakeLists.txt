cmake_minimum_required(VERSION 3.16)
project(a_mongoose_engine C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find SDL3 and OpenGL
find_package(SDL3 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

# Bring in the sibling asyncinput library
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../asyncinput ${CMAKE_CURRENT_BINARY_DIR}/asyncinput_build)

include_directories(
    ${CMAKE_CURRENT_LIST_DIR}/../asyncinput/include
    ${CMAKE_CURRENT_LIST_DIR}/src
)

add_executable(curve_paint
    src/main.c
    src/gl_loader.c
)

# Prefer the shared lib if enabled, fall back to static otherwise
if(TARGET asyncinput_shared)
    target_link_libraries(curve_paint PRIVATE asyncinput_shared)
elseif(TARGET asyncinput_static)
    target_link_libraries(curve_paint PRIVATE asyncinput_static)
else()
    message(FATAL_ERROR "asyncinput library targets not found")
endif()

# SDL3 + OpenGL + Threads
if(TARGET OpenGL::GL)
    target_link_libraries(curve_paint PRIVATE OpenGL::GL)
else()
    target_link_libraries(curve_paint PRIVATE GL)
endif()

target_link_libraries(curve_paint PRIVATE SDL3::SDL3 Threads::Threads)

# Compile definitions for SDL3 callback main style
add_compile_definitions(SDL_MAIN_USE_CALLBACKS=1)

# On Linux, ensure we link with dl for GL symbol lookup if needed
if(UNIX AND NOT APPLE)
    target_link_libraries(curve_paint PRIVATE dl)
endif()
