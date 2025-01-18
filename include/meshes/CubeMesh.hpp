#ifndef CUBE_MESH_HPP
#define CUBE_MESH_HPP

#include "Mesh.hpp"

class CubeMesh : public Mesh {
public:
    CubeMesh(int size);
    ~CubeMesh();

    void render();
};
#endif // CUBE_MESH_HPP