#ifndef CIRCLE_HPP
#define CIRCLE_HPP

#include "Mesh.hpp"
#include <vector>
#define _USE_MATH_DEFINES
#include <cmath>

class Circle : public Mesh {
public:
    Circle(int radius);
    ~Circle();

    void render();
private:
    unsigned int VBO, VAO, EBO;
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
};
#endif // CIRCLE_HPP