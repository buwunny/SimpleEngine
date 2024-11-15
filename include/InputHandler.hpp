#ifndef INPUTHANDLER_HPP
#define INPUTHANDLER_HPP

#include "../include/Camera.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

class InputHandler {
public:
    InputHandler(Camera* camera);
    ~InputHandler();

    void processInput(GLFWwindow *window, float deltaTime);
    void processMouse(float xpos, float ypos);
    static void mouse_callback(GLFWwindow* window, double xpos, double ypos);
private:
    Camera* camera;
    float lastX, lastY;
    bool firstMouse;
};
#endif // INPUTHANDLER_HPP