cmake_minimum_required(VERSION 3.20)
project(DesktopFly LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
    add_compile_options(/W4 /EHsc /utf-8 /permissive- /Zc:preprocessor)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external
)

set(SOURCES
    src/main.cpp
    src/Sim.cpp
    src/SimTest.cpp
    src/Geometry.cpp
    src/RendererD3D11.cpp
    src/FlyModel.cpp
    src/Snapshot.cpp
    src/Environment.cpp
    src/DCompOverlay.cpp
    src/Coordinator.cpp
    src/BrainView.cpp
    src/BehaviorTest.cpp
)

set(HEADERS
    src/Sim.hpp
    src/SignalBuilder.hpp
    src/Geometry.hpp
    src/RendererD3D11.hpp
    src/FlyModel.hpp
    src/Environment.hpp
    src/DCompOverlay.hpp
    src/Coordinator.hpp
    src/BrainView.hpp
)

add_executable(DesktopFly ${SOURCES} ${HEADERS})

target_link_libraries(DesktopFly PRIVATE
    d3d11
    dxgi
    d3dcompiler
    dcomp
    dwrite
    d2d1
    user32
    gdi32
    shell32
)

# Copy data/ and shaders/ directories to output directory
add_custom_command(TARGET DesktopFly POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/data
    $<TARGET_FILE_DIR:DesktopFly>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders
    $<TARGET_FILE_DIR:DesktopFly>/shaders
)
