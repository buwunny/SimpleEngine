#ifndef OBJECT_HPP
#define OBJECT_HPP

#include "../colliders/Collider.hpp"
#include "../meshes/Mesh.hpp"
#include "../Window.hpp"
#include "../Shader.hpp"

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>

class Object {
public:
    virtual void render(Window& window, Shader& shader) = 0;
    virtual void renderTransparent(Window& window, Shader& shader) = 0;
    virtual void renderFill(Window& window, Shader& shader) = 0;
    virtual void checkCollision(Object* other) = 0;
    glm::mat4 getModel() { return model; };
    glm::vec4 getColor() { return color; };
    Collider* getCollider() { return collider; };
    Mesh* getMesh() { return mesh; };
    void setModel(glm::mat4 model) { this->model = model; };
    void setColor(glm::vec4 color) { this->color = color; };
protected:
    Collider* collider;
    Mesh* mesh;
    glm::vec4 color;
    glm::mat4 model;
    bool wireframe;
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
};
#endif // OBJECT_HPP