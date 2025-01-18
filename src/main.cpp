#include <GL/glew.h>
#include <iostream>

#include "../include/Shader.hpp"
#include "../include/colliders/CubeCollider.hpp"
#include "../include/objects/Cube.hpp"
#include "../include/Camera.hpp"
#include "../include/InputHandler.hpp"
#include "../include/Window.hpp"

float deltaTime = 0.0f;
float lastFrame = 0.0f;

int main()
{
    Window window(800, 600, "Spinning Cube");
    Camera playerCamera(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    InputHandler inputHandler(&playerCamera);
    CubeCollider playerCollider(glm::vec3(0.0f), 1.0f);

    glfwSetWindowUserPointer(window.getWindow(), &inputHandler);
    glfwSetCursorPosCallback(window.getWindow(), inputHandler.mouse_callback);    

    Object* objects[4];
    Cube cube1(1);
    Cube cube2(2, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 3.0f, 0.0f)), glm::vec4(0.0f, 0.5f, 0.5f, 1.0f));
    Cube cube3(2, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 6.0f, 0.0f)), glm::vec4(0.5f, 0.5f, 0.0f, 1.0f));
    Cube cube4(2, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 9.0f, 0.0f)), glm::vec4(0.5f, 0.0f, 0.5f, 1.0f));
    objects[0] = &cube1;
    objects[1] = &cube2;
    objects[2] = &cube3;
    objects[3] = &cube4;
    Shader shader("../shaders/vertex.glsl", "../shaders/fragment.glsl");

    while (!window.shouldClose())
    {
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        inputHandler.processInput(&window, deltaTime);

        playerCollider.setPosition(playerCamera.getPosition());
        for (int i = 0; i < 4; i++) {
            if(playerCollider.checkCollision(*objects[i]->getCollider())) {
                std::cout << "Collision detected!" << std::endl;
            }
            else {
                std::cout << "Player position: " << playerCamera.getPosition().x << ", " << playerCamera.getPosition().y << ", " << playerCamera.getPosition().z << std::endl;
            } 
        }

    

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shader.use();

        glm::mat4 view = glm::lookAt(playerCamera.getPosition(), playerCamera.getPosition() + playerCamera.getFront(), playerCamera.getUp());
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 0.1f, 100.0f);
        shader.setViewMatrix(view);
        shader.setProjectionMatrix(projection);

        cube1.setModel(glm::rotate(glm::mat4(1.0f), (float)glfwGetTime(), glm::vec3(0.5f, 1.0f, 0.0f)));
        cube1.render(window, shader);

        cube2.render(window, shader);
        cube3.renderTransparent(window, shader);
        cube4.renderFill(window, shader);
        // plane.setModel(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)));
        // renderMeshWireframe(plane, window, shader, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
        
        // glm::mat4 model4 = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 10.0f, -50.0f));
        // model4 = glm::rotate(model4, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        // circle.setModel(model4);
        // renderMeshFill(circle, window, shader, glm::vec4(1.0f, 0.5f, 0.0f, 1.0f));

        window.update();
    }
    return 0;
}