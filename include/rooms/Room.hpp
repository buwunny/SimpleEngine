#ifndef ROOM_HPP
#define ROOM_HPP

#include "../objects/Object.hpp"
#include "../Shader.hpp"
#include "../Camera.hpp"
#include "../Window.hpp"

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>

class Room {
public:
    void addRigidBodiesToWorld(btDynamicsWorld *dynamicsWorld) {
        for (auto& object : objects) {
            dynamicsWorld->addRigidBody(object->getRigidBody());
        }
    };
    void update() {
        for (auto& object : objects) {
            object->update();
        }
    };
    void render(Window& window, Shader& shader) {
        for (auto& object : objects) {
            object->render(window, shader);
        }
    };
    void renderTransparent(Window& window, Shader& shader) {
        for (auto& object : objects) {
            object->renderTransparent(window, shader);
        }
    };
    void renderFill(Window& window, Shader& shader) {
        for (auto& object : objects) {
            object->renderFill(window, shader);
        }
    };

protected:
    std::vector<Object*> objects;
};

#endif // ROOM_HPP