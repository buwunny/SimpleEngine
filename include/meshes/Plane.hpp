#ifndef PLANE_HPP
#define PLANE_HPP

#include "Mesh.hpp"

class Plane : public Mesh {
public:
    Plane(int length, int width, int subdivisions);
    ~Plane();

    void render();
};
#endif // PLANE_HPP