cmake_minimum_required(VERSION 3.10)
project(OpenGLProject)

set(CMAKE_CXX_STANDARD 11)

set(SOURCES
    src/main.cpp
    src/Shader.cpp
    src/colliders/CubeCollider.cpp
    src/meshes/PlaneMesh.cpp
    src/meshes/CubeMesh.cpp
    src/meshes/CircleMesh.cpp
    src/objects/Cube.cpp
    src/objects/Plane.cpp
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

add_custom_command(TARGET OpenGLProject POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/src/shaders $<TARGET_FILE_DIR:OpenGLProject>/shaders
)