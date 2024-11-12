#include "../include/InputHandler.hpp"

InputHandler::InputHandler(Window& window, Camera& camera) : window(window), camera(camera) {
    previousTime = 0.0;
    mouseX = 0.0, mouseY = 0.0;
    lastMouseX = 0.0, lastMouseY = 0.0;
}

void InputHandler::update() {
    double currentTime = glfwGetTime();
    double deltaTime = currentTime - previousTime;
    previousTime = currentTime;
    glfwPollEvents();
    
    // WASD Movement
    float movementSpeed = 5.0f;
    glm::vec3 movement(0.0f);
    if (window.isKeyPressed(GLFW_KEY_LEFT_CONTROL)) {
        movementSpeed = movementSpeed / 4;
    }
    if (window.isKeyPressed(GLFW_KEY_W)) {
        movement += camera.getFront() * static_cast<float>(deltaTime) * movementSpeed;
    }
    if (window.isKeyPressed(GLFW_KEY_S)) {
        movement -= camera.getFront() * static_cast<float>(deltaTime) * movementSpeed;
    }
    if (window.isKeyPressed(GLFW_KEY_A)) {
        movement -= camera.getRight() * static_cast<float>(deltaTime) * movementSpeed;
    }
    if (window.isKeyPressed(GLFW_KEY_D)) {
        movement += camera.getRight() * static_cast<float>(deltaTime) * movementSpeed;
    }
    camera.update(camera.getPosition() +movement, camera.getFront(), camera.getUp());
    
    // Camera Control
    window.getCursorPosition(mouseX, mouseY);
    
    double xOffset = mouseX - lastMouseX;
    double yOffset = lastMouseY - mouseY; 
    
    lastMouseX = mouseX;
    lastMouseY = mouseY;
    
    const float sensitivity = 0.1f;
    xOffset *= sensitivity;
    yOffset *= sensitivity;
    
    camera.rotate((float)xOffset, (float)yOffset);
    
    previousTime = glfwGetTime();
}