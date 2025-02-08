#include "../../include/objects/Plane.hpp"

Plane::Plane(int size, glm::mat4 model, glm::vec4 color, float mass) {
    mesh = new PlaneMesh(size, size, 1);
    collisionShape = new btBoxShape(btVector3(size / 2.0f, 0.01f, size / 2.0f));
    btVector3 localInertia(0, 0, 0);
    if (mass != 0.0f) {
        collisionShape->calculateLocalInertia(mass, localInertia);
    }
    btTransform transform;
    transform.setFromOpenGLMatrix(glm::value_ptr(model));
    btDefaultMotionState* motionState = new btDefaultMotionState(transform);
    btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, motionState, collisionShape, localInertia);
    rigidBody = new btRigidBody(rbInfo);
    this->model = model;
    this->color = color;
}


Plane::~Plane() {
    delete mesh;
    delete collisionShape;
    delete rigidBody;
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