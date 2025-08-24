cmake_minimum_required(VERSION 3.16)
project(scene_roundtrip_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Paths
set(FLECS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/flecs/include")

# Dependencies
find_package(yaml-cpp QUIET)
if (NOT yaml-cpp_FOUND)
  message(STATUS "yaml-cpp not found via CMake. Attempting to link system yaml-cpp")
endif()

add_executable(scene_roundtrip
  src/scene_model.hpp
  src/scene_yaml.cpp
  src/world_interop.cpp
  src/roundtrip_main.cpp
)

# Treat headers in add_executable for IDEs; no special compile
set_source_files_properties(src/scene_model.hpp PROPERTIES HEADER_FILE_ONLY ON)

# Includes and defines
# Define FLECS_NO_CPP to make sure we use the C API of Flecs inside C++
target_compile_definitions(scene_roundtrip PRIVATE FLECS_NO_CPP=1)

target_include_directories(scene_roundtrip PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${FLECS_INCLUDE_DIR}
)

# Link dependencies
if (yaml-cpp_FOUND)
  target_link_libraries(scene_roundtrip PRIVATE yaml-cpp)
else()
  find_library(YAMLCPP_LIB yaml-cpp)
  if (YAMLCPP_LIB)
    target_link_libraries(scene_roundtrip PRIVATE ${YAMLCPP_LIB})
  else()
    message(FATAL_ERROR "yaml-cpp is required. Please install yaml-cpp (e.g. pacman -S yaml-cpp).")
  endif()
endif()

# Link Flecs from third_party if available
set(FLECS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/flecs")
if (EXISTS "${FLECS_ROOT}/libflecs_static.a")
  message(STATUS "Linking Flecs static library from ${FLECS_ROOT}")
  find_package(Threads REQUIRED)
  target_link_libraries(scene_roundtrip PRIVATE "${FLECS_ROOT}/libflecs_static.a" Threads::Threads dl m)
elseif (EXISTS "${FLECS_ROOT}/libflecs.so")
  message(STATUS "Linking Flecs shared library from ${FLECS_ROOT}")
  find_package(Threads REQUIRED)
  target_link_libraries(scene_roundtrip PRIVATE "${FLECS_ROOT}/libflecs.so" Threads::Threads dl m)
else()
  message(FATAL_ERROR "Flecs library not found in ${FLECS_ROOT}. Build or provide flecs (libflecs_static.a or libflecs.so).")
endif()

# Link Flecs from third_party if available
set(FLECS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/flecs")
if (EXISTS "${FLECS_ROOT}/libflecs_static.a")
  message(STATUS "Linking Flecs static library from ${FLECS_ROOT}")
  find_package(Threads REQUIRED)
  target_link_libraries(scene_roundtrip PRIVATE "${FLECS_ROOT}/libflecs_static.a" Threads::Threads dl m)
elseif (EXISTS "${FLECS_ROOT}/libflecs.so")
  message(STATUS "Linking Flecs shared library from ${FLECS_ROOT}")
  find_package(Threads REQUIRED)
  target_link_libraries(scene_roundtrip PRIVATE "${FLECS_ROOT}/libflecs.so" Threads::Threads dl m)
else()
  message(FATAL_ERROR "Flecs library not found in ${FLECS_ROOT}. Build or provide flecs (libflecs_static.a or libflecs.so).")
endif()

# Link Flecs from third_party if available
set(FLECS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/flecs")
if (EXISTS "${FLECS_ROOT}/libflecs_static.a")
  message(STATUS "Linking Flecs static library from ${FLECS_ROOT}")
  find_package(Threads REQUIRED)
  target_link_libraries(scene_roundtrip PRIVATE "${FLECS_ROOT}/libflecs_static.a" Threads::Threads dl m)
elseif (EXISTS "${FLECS_ROOT}/libflecs.so")
  message(STATUS "Linking Flecs shared library from ${FLECS_ROOT}")
  find_package(Threads REQUIRED)
  target_link_libraries(scene_roundtrip PRIVATE "${FLECS_ROOT}/libflecs.so" Threads::Threads dl m)
else()
  message(FATAL_ERROR "Flecs library not found in ${FLECS_ROOT}. Build or provide flecs (libflecs_static.a or libflecs.so).")
endif()

# Link Flecs from third_party if available
set(FLECS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/flecs")
if (EXISTS "${FLECS_ROOT}/libflecs_static.a")
  message(STATUS "Linking Flecs static library from ${FLECS_ROOT}")
  find_package(Threads REQUIRED)
  target_link_libraries(scene_roundtrip PRIVATE "${FLECS_ROOT}/libflecs_static.a" Threads::Threads dl m)
elseif (EXISTS "${FLECS_ROOT}/libflecs.so")
  message(STATUS "Linking Flecs shared library from ${FLECS_ROOT}")
  find_package(Threads REQUIRED)
  target_link_libraries(scene_roundtrip PRIVATE "${FLECS_ROOT}/libflecs.so" Threads::Threads dl m)
else()
  message(FATAL_ERROR "Flecs library not found in ${FLECS_ROOT}. Build or provide flecs (libflecs_static.a or libflecs.so).")
endif()
