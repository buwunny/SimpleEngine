#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>


#include <fstream>
#include <sstream>
#include <iostream>

#include "../include/Window.hpp"
#include "../include/Shader.hpp"
#include "../include/Renderer.hpp"

int main() {
    Window window(800, 600, "OpenGL");
    
    Shader shader("../../shaders/vertexShader.glsl", "../../shaders/fragmentShader.glsl");

    Renderer renderer(window, shader);
    
    while (!window.shouldClose()) {
        renderer.render();
    }
    return 0;
}