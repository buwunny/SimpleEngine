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
#include "../include/InputHandler.hpp"
#include "../include/Camera.hpp"

int main() {
    Window window(800, 600, "OpenGL");
    Camera camera(window);

    Shader shader("../../shaders/vertexShader.glsl", "../../shaders/fragmentShader.glsl");

    Renderer renderer(window, shader, camera);
    InputHandler inputHandler(window, camera);

    
    while (!window.shouldClose()) {
        inputHandler.update();
        renderer.render();
        window.swapBuffers();
    }
    return 0;
}