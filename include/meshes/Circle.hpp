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
};
#endif // CIRCLE_HPP