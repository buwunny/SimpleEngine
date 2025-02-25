#ifndef PLAYER_HPP
#define PLAYER_HPP

#include "../Window.hpp"
#include "../Camera.hpp"
#include "../InputHandler.hpp"
#include "Object.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <btBulletDynamicsCommon.h>

class Player : public Object {
public:
    Player(Camera* camera, glm::mat4 model = glm::mat4(1.0f));
    ~Player();

    bool isOnGround(btDiscreteDynamicsWorld* dynamicsWorld);
    void processInput(Window *window, float deltaTime, btDiscreteDynamicsWorld* dynamicsWorld);
    void processMouse(float xpos, float ypos);
    static void mouse_callback(GLFWwindow* window, double xpos, double ypos);
    void render(Window& window, Shader& shader) {};
    void renderTransparent(Window& window, Shader& shader) {};
    void renderFill(Window& window, Shader& shader) {};
private:
    Camera* camera;
    InputHandler* inputHandler;
    float mass;
    float movementSpeed;
    float lastX, lastY;
    bool firstMouse;
};
#endif // PLAYER_HPP