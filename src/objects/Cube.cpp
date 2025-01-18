#include "../../include/objects/Cube.hpp"

Cube::Cube(int size, glm::mat4 model, glm::vec4 color) {
    mesh = new CubeMesh(size);

    collider = new CubeCollider(glm::vec3(model[3]), size);
    this->model = model;
    this->color = color;
}


Cube::~Cube() {
    delete mesh;
    delete collider;
}

void Cube::render(Window& window, Shader& shader) {
    shader.setModelMatrix(model);
    window.setPolygonMode(GL_FILL);
    shader.setFragmentColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    mesh->render();
    window.setPolygonMode(GL_LINE);
    window.setLineWidth(3.0f);
    shader.setFragmentColor(color);
    mesh->render();;
}

void Cube::renderTransparent(Window& window, Shader& shader) {
    shader.setModelMatrix(model);
    window.setPolygonMode(GL_LINE);
    window.setLineWidth(3.0f);
    shader.setFragmentColor(color);
    mesh->render();;
}

void Cube::renderFill(Window& window, Shader& shader) {
    shader.setModelMatrix(model);
    window.setPolygonMode(GL_FILL);
    shader.setFragmentColor(color);
    mesh->render();
}

void Cube::checkCollision(Object* other) {
    if (collider->checkCollision(*other->getCollider())) {
        std::cout << "Collision detected!" << std::endl;
    }
}