#include "../include/InputHandler.hpp"

InputHandler::InputHandler(Camera* camera) {
    lastX = 400;
    lastY = 300;
    firstMouse = true;
    this->camera = camera;
}

InputHandler::~InputHandler() {
}

void InputHandler::processInput(GLFWwindow *window, float deltaTime) {
    glm::vec3 cameraPos = camera->getPosition();
    glm::vec3 cameraFront = camera->getFront();
    glm::vec3 cameraUp = camera->getUp();
    float cameraSpeed = 2.5f * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        cameraPos += cameraSpeed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        cameraPos -= cameraSpeed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
    camera->setPosition(cameraPos);
}

void InputHandler::processMouse(float xpos, float ypos) {
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos; // reversed since y-coordinates go from bottom to top
    lastX = xpos;
    lastY = ypos;
    
    this->camera->look(xoffset, yoffset);
}

void InputHandler::mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    InputHandler* inputHandler = static_cast<InputHandler*>(glfwGetWindowUserPointer(window));
    if (inputHandler) {
        inputHandler->processMouse(xpos, ypos);
    }
}

