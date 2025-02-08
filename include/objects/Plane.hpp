#ifndef PLANE_HPP
#define PLANE_HPP

#include "Object.hpp"
#include "../meshes/PlaneMesh.hpp"

class Plane : public Object {
public:
    Plane(int size, glm::mat4 model = glm::mat4(1.0f), glm::vec4 color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), float mass = 0.0f);
    ~Plane();

    void render(Window& window, Shader& shader);
    void renderTransparent(Window& window, Shader& shader);
    void renderFill(Window& window, Shader& shader);
};
#endif // PLANE_HPP