cmake_minimum_required(VERSION 3.10)
project(MyProject)

add_executable(MyExecutable 
    ../src/main.cpp
    ../src/Shader.cpp
    ../src/Window.cpp
    ../src/Renderer.cpp
    ../src/InputHandler.cpp
    ../src/Camera.cpp)