#include <GL/glew.h>

#include "../include/Shader.hpp"
#include "../include/meshes/Mesh.hpp"
#include "../include/meshes/Mesh.hpp"
#include "../include/meshes/Cube.hpp"
#include "../include/Camera.hpp"
#include "../include/InputHandler.hpp"
#include "../include/Window.hpp"

float deltaTime = 0.0f;
float lastFrame = 0.0f;

void renderMesh(Mesh& mesh, Window& window, Shader& shader)
{
    window.setPolygonMode(GL_FILL);
    shader.setFragmentColor(0.0f, 0.0f, 0.0f, 1.0f);
    mesh.render();
    window.setPolygonMode(GL_LINE);
    window.setLineWidth(3.0f);
    shader.setFragmentColor(0.5f, 0.0f, 0.5f, 1.0f);
    mesh.render();
}

int main()
{
    Window window(800, 600, "Spinning Cube");
    Camera camera(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    InputHandler inputHandler(&camera);

    glfwSetWindowUserPointer(window.getWindow(), &inputHandler);
    glfwSetCursorPosCallback(window.getWindow(), inputHandler.mouse_callback);    

    Cube cube(1);
    Cube cube2(2);
    Shader shader("../shaders/vertex.glsl", "../shaders/fragment.glsl");

    while (!window.shouldClose())
    {
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        inputHandler.processInput(window.getWindow(), deltaTime);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shader.use();

        glm::mat4 view = glm::lookAt(camera.getPosition(), camera.getPosition() + camera.getFront(), camera.getUp());
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 0.1f, 100.0f);
        shader.setViewMatrix(view);
        shader.setProjectionMatrix(projection);

        glm::mat4 model1 = glm::rotate(glm::mat4(1.0f), (float)glfwGetTime(), glm::vec3(0.5f, 1.0f, 0.0f));
        shader.setModelMatrix(model1);
        renderMesh(cube, window, shader);

        glm::mat4 model2 = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 3.0f, 0.0f));
        shader.setModelMatrix(model2);
        renderMesh(cube2, window, shader);

        window.update();
    }
    return 0;
}