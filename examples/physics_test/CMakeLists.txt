add_executable(physics_test main.cpp)
target_link_libraries(physics_test PRIVATE unitylike flecs)
target_include_directories(physics_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../.. include third_party/flecs/include)
