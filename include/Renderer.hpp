#ifndef RENDERER_HPP
#define RENDERER_HPP

#include "Shader.hpp"
#include "Window.hpp"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>


class Renderer {
public:
    Renderer(Window& window, Shader& shader);
    ~Renderer();
    void render();

private:
    Window& window;
    Shader& shader;
    unsigned int VAO;
    unsigned int VBO;
    unsigned int EBO;
};

#endif // RENDERER_HPP