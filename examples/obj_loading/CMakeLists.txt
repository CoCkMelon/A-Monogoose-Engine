cmake_minimum_required(VERSION 3.16)
project(obj_loading CXX)

add_executable(obj_loading
  main.cpp
)

set_target_properties(obj_loading PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

if(TARGET OpenGL::GL)
  target_link_libraries(obj_loading PRIVATE OpenGL::GL)
else()
  target_link_libraries(obj_loading PRIVATE GL)
endif()

target_link_libraries(obj_loading PRIVATE 
  ${AME_SDL3_TARGET}
  Threads::Threads 
  ame 
  unitylike
  flecs 
  ${AME_SDL3_IMAGE_TARGET}
  glad
)

if(UNIX AND NOT APPLE)
  target_link_libraries(obj_loading PRIVATE dl m)
endif()
