#ifndef PLANE_HPP
#define PLANE_HPP

#include "Mesh.hpp"
#include <vector>

class Plane : public Mesh {
public:
    Plane(int length, int width, int subdivisions);
    ~Plane();

    void render();
private:
    unsigned int VBO, VAO, EBO;    
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
};
#endif // PLANE_HPP