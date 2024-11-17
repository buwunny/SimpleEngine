#ifndef MESH_HPP
#define MESH_HPP

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>

class Mesh {
public:
    Mesh();
    ~Mesh();

    virtual void render() = 0;
    glm::mat4 getModel() { return model; };
    void setModel(glm::mat4 model) { this->model = model; };
    bool isWireframe() { return wireframe; };
protected:
    unsigned int VBO, VAO, EBO;
    glm::mat4 model;
    bool wireframe;
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
};
#endif // MESH_HPP