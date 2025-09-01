cmake_minimum_required(VERSION 3.16)
project(unitylike_platformer_ecs CXX)

add_executable(unitylike_platformer_ecs
  main.cpp
  input_local.cpp
)

set_target_properties(unitylike_platformer_ecs PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

if(TARGET OpenGL::GL)
  target_link_libraries(unitylike_platformer_ecs PRIVATE OpenGL::GL)
else()
  target_link_libraries(unitylike_platformer_ecs PRIVATE GL)
endif()

target_link_libraries(unitylike_platformer_ecs PRIVATE 
  ${AME_SDL3_TARGET}
  Threads::Threads 
  unitylike 
  ame 
  flecs 
  ${AME_SDL3_IMAGE_TARGET}
  glad
)

# asyncinput selection mirroring top-level
if(TARGET asyncinput_shared)
  target_link_libraries(unitylike_platformer_ecs PRIVATE asyncinput_shared)
elseif(TARGET asyncinput_static)
  target_link_libraries(unitylike_platformer_ecs PRIVATE asyncinput_static)
elseif(TARGET asyncinput)
  target_link_libraries(unitylike_platformer_ecs PRIVATE asyncinput)
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(unitylike_platformer_ecs PRIVATE dl m)
endif()
