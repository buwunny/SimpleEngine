#include "../../include/objects/Plane.hpp"

Plane::Plane(int size, glm::mat4 model, glm::vec4 color) {
    mesh = new PlaneMesh(size, size, 1);
    collider = new CubeCollider(glm::vec3(model[3]), glm::vec3(size, 0.01f, size));
    this->model = model;
    this->color = color;
}


Plane::~Plane() {
    delete mesh;
    delete collider;
}

void Plane::render(Window& window, Shader& shader) {
    shader.setModelMatrix(model);
    window.setPolygonMode(GL_FILL);
    shader.setFragmentColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    mesh->render();
    window.setPolygonMode(GL_LINE);
    window.setLineWidth(3.0f);
    shader.setFragmentColor(color);
    mesh->render();;
}

void Plane::renderTransparent(Window& window, Shader& shader) {
    shader.setModelMatrix(model);
    window.setPolygonMode(GL_LINE);
    window.setLineWidth(3.0f);
    shader.setFragmentColor(color);
    mesh->render();;
}

void Plane::renderFill(Window& window, Shader& shader) {
    shader.setModelMatrix(model);
    window.setPolygonMode(GL_FILL);
    shader.setFragmentColor(color);
    mesh->render();
}

void Plane::checkCollision(Object* other) {
    if (collider->checkCollision(*other->getCollider())) {
        std::cout << "Collision detected!" << std::endl;
    }
}