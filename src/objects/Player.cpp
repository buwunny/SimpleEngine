#include "../../include/objects/Player.hpp"

Player::Player(Camera* camera, glm::mat4 model) {
    movementSpeed = 10.0f;
    lastX = 400;
    lastY = 300;
    firstMouse = true;
    this->camera = camera;
    inputHandler = new InputHandler(camera);
    collisionShape = new btCapsuleShape(0.5f, 1.0f);
    btVector3 localInertia(0, 0, 0);
    collisionShape->calculateLocalInertia(10, localInertia);

    btTransform transform;
    transform.setFromOpenGLMatrix(glm::value_ptr(model));
    btDefaultMotionState* motionState = new btDefaultMotionState(transform);

    btRigidBody::btRigidBodyConstructionInfo rbInfo(10, motionState, collisionShape, localInertia);
    rigidBody = new btRigidBody(rbInfo);
    rigidBody->setAngularFactor(btVector3(0, 0, 0));
}

Player::~Player() {
    delete inputHandler;
    delete collisionShape;
    delete rigidBody;
}

void Player::processInput(Window *window, float deltaTime) {
    float cameraSpeed = movementSpeed * deltaTime * 1000;
    if (window->isKeyPressed(GLFW_KEY_LEFT_SHIFT))
        cameraSpeed *= 2;

    btVector3 velocity(0, 0, 0);
    btVector3 forwardDir = btVector3(camera->getFront().x, 0, camera->getFront().z);
    btVector3 rightDir = btVector3(camera->getRight().x, 0, camera->getRight().z);

    if (window->isKeyPressed(GLFW_KEY_W))
        velocity += forwardDir * cameraSpeed;
    if (window->isKeyPressed(GLFW_KEY_S))
        velocity -= forwardDir * cameraSpeed;
    if (window->isKeyPressed(GLFW_KEY_A))
        velocity -= rightDir * cameraSpeed;
    if (window->isKeyPressed(GLFW_KEY_D))
        velocity += rightDir * cameraSpeed;
    if (window->isKeyPressed(GLFW_KEY_ESCAPE))
        window->close();
    if (window->isKeyPressed(GLFW_KEY_SPACE)) {
        btVector3 jumpVelocity = rigidBody->getLinearVelocity();
        if (jumpVelocity.getY() == 0) {
            velocity.setY(10.0f);
        }
    }
    else {
        velocity.setY(rigidBody->getLinearVelocity().getY());
    }

    rigidBody->activate(true);
    rigidBody->setLinearVelocity(velocity);

    btTransform trans;
    rigidBody->getMotionState()->getWorldTransform(trans);
    btVector3 pos = trans.getOrigin();
    camera->setPosition(glm::vec3(pos.getX(), pos.getY() + 1.0f, pos.getZ()));
}

void Player::processMouse(float xpos, float ypos) {
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos;
    lastX = xpos;
    lastY = ypos;
    
    this->camera->look(xoffset, yoffset);
}

void Player::mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    Player* player = static_cast<Player*>(glfwGetWindowUserPointer(window));
    if (player) {
        player->processMouse(xpos, ypos);
    }
}
