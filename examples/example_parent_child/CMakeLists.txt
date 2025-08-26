cmake_minimum_required(VERSION 3.16)
project(example_parent_child CXX)

add_executable(example_parent_child
  main.cpp
)

set_target_properties(example_parent_child PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

if(TARGET OpenGL::GL)
  target_link_libraries(example_parent_child PRIVATE OpenGL::GL)
else()
  target_link_libraries(example_parent_child PRIVATE GL)
endif()

target_link_libraries(example_parent_child PRIVATE SDL3::SDL3 Threads::Threads unitylike ame flecs)

if(TARGET asyncinput_shared)
  target_link_libraries(example_parent_child PRIVATE asyncinput_shared)
elseif(TARGET asyncinput_static)
  target_link_libraries(example_parent_child PRIVATE asyncinput_static)
elseif(TARGET asyncinput)
  target_link_libraries(example_parent_child PRIVATE asyncinput)
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(example_parent_child PRIVATE dl)
endif()

