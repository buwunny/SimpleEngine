#include "../../include/objects/Player.hpp"

Player::Player(Camera* camera, glm::mat4 model) {
    movementSpeed = 100.0f;
    lastX = 1920/2;
    lastY = 1080/2;
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

    rigidBody->setCcdMotionThreshold(0.5);
    rigidBody->setCcdSweptSphereRadius(0.4);

    rigidBody->setFriction(2.0f);
}

Player::~Player() {
    delete inputHandler;
    delete collisionShape;
    delete rigidBody;
}

bool Player::isOnGround(btDiscreteDynamicsWorld* dynamicsWorld) {
    btVector3 start = rigidBody->getWorldTransform().getOrigin();
    btVector3 end = start - btVector3(0, 1.05f, 0);
    btCollisionWorld::ClosestRayResultCallback rayCallback(start, end);
    dynamicsWorld->rayTest(start, end, rayCallback);
    return rayCallback.hasHit();
}

void Player::processInput(Window *window, float deltaTime, btDiscreteDynamicsWorld* dynamicsWorld) {
    float cameraSpeed = movementSpeed * deltaTime;

    btVector3 velocity = rigidBody->getLinearVelocity();
    btVector3 forwardDir = btVector3(camera->getFront().x, 0, camera->getFront().z).normalized();
    btVector3 rightDir = btVector3(camera->getRight().x, 0, camera->getRight().z).normalized();

    bool isOnGround = this->isOnGround(dynamicsWorld);
    float adjustedSpeed = isOnGround ? cameraSpeed : cameraSpeed * 0.1f;

    if (window->isKeyPressed(GLFW_KEY_W))
        velocity += forwardDir * adjustedSpeed;
    if (window->isKeyPressed(GLFW_KEY_S))
        velocity -= forwardDir * adjustedSpeed;
    if (window->isKeyPressed(GLFW_KEY_A))
        velocity -= rightDir * adjustedSpeed;
    if (window->isKeyPressed(GLFW_KEY_D))
        velocity += rightDir * adjustedSpeed; 
    if (window->isKeyPressed(GLFW_KEY_ESCAPE))
        window->close();
    if (window->isKeyPressed(GLFW_KEY_SPACE) && isOnGround)
        velocity.setY(10.0f);

    rigidBody->activate(true);
    float maxSpeed = 25.0f;
    if (window->isKeyPressed(GLFW_KEY_LEFT_SHIFT))
        maxSpeed *= 2;
    if (velocity.length() > maxSpeed) {
        velocity = velocity.normalized() * maxSpeed;
    }
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