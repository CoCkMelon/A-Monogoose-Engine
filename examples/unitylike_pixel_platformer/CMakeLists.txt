cmake_minimum_required(VERSION 3.16)
project(unitylike_pixel_platformer CXX)

add_executable(unitylike_pixel_platformer
  main.cpp
  ../unitylike_minimal/input_local.cpp
)

set_target_properties(unitylike_pixel_platformer PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

if(TARGET OpenGL::GL)
  target_link_libraries(unitylike_pixel_platformer PRIVATE OpenGL::GL)
else()
  target_link_libraries(unitylike_pixel_platformer PRIVATE GL)
endif()

target_link_libraries(unitylike_pixel_platformer PRIVATE SDL3::SDL3 Threads::Threads unitylike ame flecs SDL3_image::SDL3_image)

# asyncinput selection mirroring top-level
if(TARGET asyncinput_shared)
  target_link_libraries(unitylike_pixel_platformer PRIVATE asyncinput_shared)
elseif(TARGET asyncinput_static)
  target_link_libraries(unitylike_pixel_platformer PRIVATE asyncinput_static)
elseif(TARGET asyncinput)
  target_link_libraries(unitylike_pixel_platformer PRIVATE asyncinput)
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(unitylike_pixel_platformer PRIVATE dl)
endif()

