#ifndef PLANE_MESH_HPP
#define PLANE_MESH_HPP

#include "Mesh.hpp"

class PlaneMesh : public Mesh {
public:
    PlaneMesh(int length, int width, int subdivisions);
    ~PlaneMesh();

    void render();
};
#endif // PLANE_HPP