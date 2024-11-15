#ifndef CUBE_HPP
#define CUBE_HPP

#include <GL/glew.h>

class Cube {
public:
    Cube(int);
    ~Cube();

    void render();
private:
    unsigned int VBO, VAO, EBO;
};
#endif // CUBE_HPP