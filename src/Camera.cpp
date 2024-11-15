#include "../include/Camera.hpp"

Camera::Camera(glm::vec3 position, glm::vec3 front, glm::vec3 up) {
    this->position = position;
    this->front = front;
    this->up = up;
    this->yaw = -90.0f;
    this->pitch = 0.0f;
}

Camera::~Camera() {
}

void Camera::look(float xoffset, float yoffset) {
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    yaw += xoffset;
    pitch += yoffset;

    if (pitch > 89.0f)
        pitch = 89.0f;
    if (pitch < -89.0f)
        pitch = -89.0f;

    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    this->front = glm::normalize(front);
}
