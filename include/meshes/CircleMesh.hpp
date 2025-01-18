#ifndef CIRCLE_MESH_HPP
#define CIRCLE_MESH_HPP

#include "Mesh.hpp"
#include <vector>
#define _USE_MATH_DEFINES
#include <cmath>

class CircleMesh : public Mesh {
public:
    CircleMesh(int radius);
    ~CircleMesh();

    void render();
};
#endif // CIRCLE_MESH_HPP