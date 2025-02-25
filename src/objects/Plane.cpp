#include "../../include/objects/Plane.hpp"

Plane::Plane(float length, float width, glm::mat4 model, glm::vec4 color, float mass) {
    mesh = new PlaneMesh(length, width, length / 5.0f, width / 5.0f);
    collisionShape = new btBoxShape(btVector3(length / 2.0f, 0.01f, width / 2.0f));
    btVector3 localInertia(0, 0, 0);
    if (mass != 0.0f) {
        collisionShape->calculateLocalInertia(mass, localInertia);
    }
    btTransform transform;
    transform.setFromOpenGLMatrix(glm::value_ptr(model));
    btDefaultMotionState* motionState = new btDefaultMotionState(transform);
    btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, motionState, collisionShape, localInertia);
    rigidBody = new btRigidBody(rbInfo);

    rigidBody->setFriction(1.0f);

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