# unitylike_audio_example: Unity-like spatial audio with touch controls
add_executable(unitylike_audio_example 
    main.cpp
    input_local.cpp
)

target_include_directories(unitylike_audio_example PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/cpp
    ${CMAKE_SOURCE_DIR}/third_party/flecs/include
)

target_link_libraries(unitylike_audio_example PRIVATE 
    unitylike 
    ame 
    flecs
)

# Link asyncinput
if(TARGET asyncinput_static)
    target_link_libraries(unitylike_audio_example PRIVATE asyncinput_static)
elseif(TARGET asyncinput_shared)
    target_link_libraries(unitylike_audio_example PRIVATE asyncinput_shared)
elseif(TARGET asyncinput)
    target_link_libraries(unitylike_audio_example PRIVATE asyncinput)
endif()

# Link SDL3
target_link_libraries(unitylike_audio_example PRIVATE ${AME_SDL3_TARGET})

# Link OpenGL
if(TARGET OpenGL::GL)
    target_link_libraries(unitylike_audio_example PRIVATE OpenGL::GL)
else()
    target_link_libraries(unitylike_audio_example PRIVATE GL)
endif()

# Additional system libraries
target_link_libraries(unitylike_audio_example PRIVATE m pthread dl)

set_target_properties(unitylike_audio_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
