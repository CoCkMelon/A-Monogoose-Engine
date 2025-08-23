cmake_minimum_required(VERSION 3.16)
project(unitylike_minimal CXX)

add_executable(unitylike_minimal
  main.cpp
  input_local.cpp
)

# Place the executable alongside other examples (e.g., build/line_draw)
# CMAKE_BINARY_DIR points to the top-level build directory
set_target_properties(unitylike_minimal PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Link to engine and façade
if(TARGET OpenGL::GL)
  target_link_libraries(unitylike_minimal PRIVATE OpenGL::GL)
else()
  target_link_libraries(unitylike_minimal PRIVATE GL)
endif()

target_link_libraries(unitylike_minimal PRIVATE SDL3::SDL3 Threads::Threads unitylike ame flecs)

# asyncinput selection mirroring top-level
if(TARGET asyncinput_shared)
  target_link_libraries(unitylike_minimal PRIVATE asyncinput_shared)
elseif(TARGET asyncinput_static)
  target_link_libraries(unitylike_minimal PRIVATE asyncinput_static)
elseif(TARGET asyncinput)
  target_link_libraries(unitylike_minimal PRIVATE asyncinput)
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(unitylike_minimal PRIVATE dl)
endif()

