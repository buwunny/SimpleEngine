#ifndef OBJECT_HPP
#define OBJECT_HPP

#include "../meshes/Mesh.hpp"
#include "../Window.hpp"
#include "../Shader.hpp"

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>
#include <btBulletDynamicsCommon.h>

class Object {
public:
    virtual void render(Window& window, Shader& shader) = 0;
    virtual void renderTransparent(Window& window, Shader& shader) = 0;
    virtual void renderFill(Window& window, Shader& shader) = 0;
    glm::mat4 getModel() { return model; };
    glm::vec4 getColor() { return color; };
    Mesh* getMesh() { return mesh; };
    void setModel(glm::mat4 model) { this->model = model; };
    btRigidBody* getRigidBody() { return rigidBody; };
    void setRigidBody(btRigidBody* rigidBody) { this->rigidBody = rigidBody; };
    btCollisionShape* getCollisionShape() { return collisionShape; };
    void setCollisionShape(btCollisionShape* shape) { this->collisionShape = shape; };

protected:
    btRigidBody* rigidBody;
    btCollisionShape* collisionShape;
    Mesh* mesh;
    glm::vec4 color;
    glm::mat4 model;
    bool wireframe;
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
};

#endif // OBJECT_HPP