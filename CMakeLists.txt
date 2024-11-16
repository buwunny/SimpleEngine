cmake_minimum_required(VERSION 3.10)
project(OpenGLProject)

set(CMAKE_CXX_STANDARD 11)

set(SOURCES
    src/main.cpp
    src/Shader.cpp
    src/meshes/Mesh.cpp
    src/meshes/Plane.cpp
    src/meshes/Cube.cpp
    src/Camera.cpp
    src/InputHandler.cpp
    src/Window.cpp
)

add_executable(OpenGLProject ${SOURCES})

set(CMAKE_PREFIX_PATH "C:/msys64/mingw64")

find_package(GLEW REQUIRED)
find_package(GLFW3 REQUIRED)
find_package(glm REQUIRED)

target_link_libraries(OpenGLProject PRIVATE GLEW::GLEW glfw glm)