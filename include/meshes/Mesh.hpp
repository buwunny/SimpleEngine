#ifndef MESH_HPP
#define MESH_HPP

#include <GL/glew.h>

class Mesh {
public:
    Mesh();
    ~Mesh();

    virtual void render() = 0;
private:
    unsigned int VBO, VAO, EBO;
};
#endif // MESH_HPP