#include <GL/glew.h>
#include <iostream>

#include <btBulletDynamicsCommon.h>
#include <btBulletCollisionCommon.h>

#include "../include/Shader.hpp"
#include "../include/objects/Cube.hpp"
#include "../include/objects/Plane.hpp"
#include "../include/objects/Player.hpp"
#include "../include/Camera.hpp"
#include "../include/InputHandler.hpp"
#include "../include/Window.hpp"

float deltaTime = 0.0f;
float lastFrame = 0.0f;

int main()
{
    btDefaultCollisionConfiguration* collisionConfiguration = new btDefaultCollisionConfiguration();
    btCollisionDispatcher* dispatcher = new btCollisionDispatcher(collisionConfiguration);
    btBroadphaseInterface* overlappingPairCache = new btDbvtBroadphase();
    btSequentialImpulseConstraintSolver* solver = new btSequentialImpulseConstraintSolver();
    btDiscreteDynamicsWorld* dynamicsWorld = new btDiscreteDynamicsWorld(dispatcher, overlappingPairCache, solver, collisionConfiguration);
    dynamicsWorld->setGravity(btVector3(0, -9.81, 0));

    Window window(800, 600, "Spinning Cube");
    Camera playerCamera(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    Player player(&playerCamera, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 3.0f, 10.0f)));
    glfwSetWindowUserPointer(window.getWindow(), &player);
    glfwSetCursorPosCallback(window.getWindow(), player.mouse_callback);    
    
    Object* objects[6];
    objects[0] = &player;

    Cube cube1(3, glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 15.0f, 0.0f)), glm::vec4(0.5f, 0.5f, 0.5f, 1.0f), 1.0f);
    Cube cube2(2, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 3.0f, 0.0f)), glm::vec4(0.0f, 0.5f, 0.5f, 1.0f), 1.0f);
    Cube cube3(2, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 6.0f, 0.0f)), glm::vec4(0.5f, 0.5f, 0.0f, 1.0f), 1.0f);
    Cube cube4(2, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 9.0f, 0.0f)), glm::vec4(0.5f, 0.0f, 0.5f, 1.0f), 1.0f);
    Plane plane(50, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -3.0f, 0.0f)), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 0.0f);
    objects[1] = &cube1;
    objects[2] = &cube2;
    objects[3] = &cube3;
    objects[4] = &cube4;
    objects[5] = &plane;

    for (int i = 0; i < 6; i++)
    {
        dynamicsWorld->addRigidBody(objects[i]->getRigidBody());
    }

    Shader shader("./shaders/vertex.glsl", "./shaders/fragment.glsl");

    while (!window.shouldClose())
    {
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        dynamicsWorld->stepSimulation(deltaTime, 10);

        player.processInput(&window, deltaTime);    

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shader.use();

        glm::mat4 view = glm::lookAt(playerCamera.getPosition(), playerCamera.getPosition() + playerCamera.getFront(), playerCamera.getUp());
        int width, height;
        glfwGetFramebufferSize(window.getWindow(), &width, &height);
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 100.0f);
        shader.setViewMatrix(view);
        shader.setProjectionMatrix(projection);

        for (int i = 0; i < 6; i++)
        {
            btTransform trans;
            objects[i]->getRigidBody()->getMotionState()->getWorldTransform(trans);
            btScalar matrix[16];
            trans.getOpenGLMatrix(matrix);
            glm::mat4 modelMatrix = glm::make_mat4(matrix);
            objects[i]->setModel(modelMatrix);
        }
        
        // cube1.setModel(glm::rotate(glm::mat4(1.0f), (float)glfwGetTime(), glm::vec3(0.5f, 1.0f, 0.0f)));
        cube1.render(window, shader);

        cube2.render(window, shader);
        cube3.renderTransparent(window, shader);
        cube4.renderFill(window, shader);
        plane.render(window, shader);
        
        window.update();
    }

    delete dynamicsWorld;
    delete solver;
    delete overlappingPairCache;
    delete dispatcher;
    delete collisionConfiguration;

    return 0;
}