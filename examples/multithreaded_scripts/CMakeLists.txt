cmake_minimum_required(VERSION 3.16)

project(multithreaded_scripts_test)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add the main executable
add_executable(multithreaded_test main.cpp)

# Link against the unitylike library and its dependencies
target_link_libraries(multithreaded_test 
    unitylike 
    flecs
    pthread
)

# Add include directories
target_include_directories(multithreaded_test PRIVATE 
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/flecs/include
)