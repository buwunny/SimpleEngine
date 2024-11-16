#ifndef CUBE_HPP
#define CUBE_HPP

#include "Mesh.hpp"

class Cube : public Mesh {
public:
    Cube(int size);
    ~Cube();

    void render();
private:
    unsigned int VBO, VAO, EBO;
};
#endif // CUBE_HPP