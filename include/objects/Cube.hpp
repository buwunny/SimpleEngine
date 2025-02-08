#ifndef CUBE_HPP
#define CUBE_HPP

#include "Object.hpp"
#include "../meshes/CubeMesh.hpp"

class Cube : public Object {
public:
    Cube(int size, glm::mat4 model = glm::mat4(1.0f), glm::vec4 color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), float mass = 10.0f);
    ~Cube();

    void render(Window& window, Shader& shader);
    void renderTransparent(Window& window, Shader& shader);
    void renderFill(Window& window, Shader& shader);
};
#endif // CUBE_HPP