#ifndef INPUTHANDLER_HPP
#define INPUTHANDLER_HPP

#include "Window.hpp"
#include "Camera.hpp"
#include "colliders/CubeCollider.hpp"
#include "objects/Object.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

class InputHandler {
public:
    InputHandler(Camera* camera);
    ~InputHandler();

    void processInput(Window *window, float deltaTime);
    void processMouse(float xpos, float ypos);
    static void mouse_callback(GLFWwindow* window, double xpos, double ypos);
    void setObjects(std::vector<Object*> objects) { this->objects = objects; }
private:
    Camera* camera;
    float movementSpeed;
    float lastX, lastY;
    bool firstMouse;
    CubeCollider* playerCollider;
    std::vector<Object*> objects;

    bool checkCollision(const glm::vec3& newPosition);
};
#endif // INPUTHANDLER_HPP