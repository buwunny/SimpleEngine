#include "../include/Camera.hpp"

Camera::Camera(Window& window)
{
    position = glm::vec3(0.0f, 0.0f, 0.0f);
    front = glm::vec3(0.0f, 0.0f, -1.0f);
    up = glm::vec3(0.0f, 1.0f, 0.0f);
    right = glm::vec3(1.0f, 0.0f, 0.0f);
    viewMatrix = glm::mat4(1.0f);
    projectionMatrix = glm::perspective(glm::radians(45.0f), static_cast<float>(window.getWidth()) / window.getHeight(), 0.1f, 100.0f);
    pitch = 0.0f, yaw = 0.0f;
}

Camera::~Camera()
{
    
}

void Camera::update(const glm::vec3& position, const glm::vec3& front, const glm::vec3& up)
{
    this->position = position;
    this->front = front;
    this->up = up;

    viewMatrix = glm::lookAt(position, position + front, up);
}

void Camera::rotate(float yaw, float pitch)
{
    this->yaw += yaw;
    this->pitch += pitch;

    if (this->pitch > 89.0f)
        this->pitch = 89.0f;
    if (this->pitch < -89.0f)
        this->pitch = -89.0f;

    glm::vec3 newFront;
    newFront.x = cos(glm::radians(this->yaw)) * cos(glm::radians(this->pitch));
    newFront.y = sin(glm::radians(this->pitch));
    newFront.z = sin(glm::radians(this->yaw)) * cos(glm::radians(this->pitch));
    front = glm::normalize(newFront);

    right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
    up = glm::normalize(glm::cross(right, front));

    viewMatrix = glm::lookAt(position, position + front, up);
}
