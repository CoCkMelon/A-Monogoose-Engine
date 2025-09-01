cmake_minimum_required(VERSION 3.16)
project(unitylike_box2d_car CXX)

add_executable(unitylike_box2d_car
  main.cpp
  input_local.cpp
)

set_target_properties(unitylike_box2d_car PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

if(TARGET OpenGL::GL)
  target_link_libraries(unitylike_box2d_car PRIVATE OpenGL::GL)
else()
  target_link_libraries(unitylike_box2d_car PRIVATE GL)
endif()

target_link_libraries(unitylike_box2d_car PRIVATE 
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
  target_link_libraries(unitylike_box2d_car PRIVATE asyncinput_shared)
elseif(TARGET asyncinput_static)
  target_link_libraries(unitylike_box2d_car PRIVATE asyncinput_static)
elseif(TARGET asyncinput)
  target_link_libraries(unitylike_box2d_car PRIVATE asyncinput)
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(unitylike_box2d_car PRIVATE dl m)
endif()

