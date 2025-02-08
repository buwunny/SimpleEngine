#ifndef INPUTHANDLER_HPP
#define INPUTHANDLER_HPP

#include "Window.hpp"
#include "Camera.hpp"
#include "objects/Object.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <btBulletDynamicsCommon.h>

class InputHandler {
public:
    InputHandler(Camera* camera);
    ~InputHandler();

    void processInput(Window *window, float deltaTime);
    void processMouse(float xpos, float ypos);
    static void mouse_callback(GLFWwindow* window, double xpos, double ypos);
private:
    Camera* camera;
    float movementSpeed;
    float lastX, lastY;
    bool firstMouse;
};
#endif // INPUTHANDLER_HPP