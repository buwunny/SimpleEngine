#include <GL/glew.h>

#include "../include/Shader.hpp"
#include "../include/meshes/Mesh.hpp"
#include "../include/meshes/Plane.hpp"
#include "../include/meshes/Cube.hpp"
#include "../include/meshes/Circle.hpp"
#include "../include/Camera.hpp"
#include "../include/InputHandler.hpp"
#include "../include/Window.hpp"

float deltaTime = 0.0f;
float lastFrame = 0.0f;

void renderMeshWireframe(Mesh& mesh, Window& window, Shader& shader, glm::vec4 color)
{
    window.setPolygonMode(GL_FILL);
    shader.setFragmentColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    mesh.render();
    window.setPolygonMode(GL_LINE);
    window.setLineWidth(3.0f);
    shader.setFragmentColor(color);
    mesh.render();
}

void renderMeshFill(Mesh& mesh, Window& window, Shader& shader, glm::vec4 color)
{
    window.setPolygonMode(GL_FILL);
    shader.setFragmentColor(color);
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
    Circle circle(10);
    Plane plane(100, 100, 25);
    Shader shader("../shaders/vertex.glsl", "../shaders/fragment.glsl");

    while (!window.shouldClose())
    {
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        inputHandler.processInput(&window, deltaTime);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shader.use();

        glm::mat4 view = glm::lookAt(camera.getPosition(), camera.getPosition() + camera.getFront(), camera.getUp());
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 0.1f, 100.0f);
        shader.setViewMatrix(view);
        shader.setProjectionMatrix(projection);

        glm::mat4 model1 = glm::rotate(glm::mat4(1.0f), (float)glfwGetTime(), glm::vec3(0.5f, 1.0f, 0.0f));
        shader.setModelMatrix(model1);
        renderMeshWireframe(cube, window, shader, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

        glm::mat4 model2 = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 3.0f, 0.0f));
        shader.setModelMatrix(model2);
        renderMeshWireframe(cube2, window, shader, glm::vec4(0.0f, 0.5f, 0.5f, 1.0f));

        glm::mat4 model3 = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f));
        shader.setModelMatrix(model3);
        renderMeshWireframe(plane, window, shader, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));

        glm::mat4 model4 = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 10.0f, -50.0f));
        model4 = glm::rotate(model4, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        shader.setModelMatrix(model4);
        renderMeshFill(circle, window, shader, glm::vec4(1.0f, 0.5f, 0.0f, 1.0f));

        window.update();
    }
    return 0;
}