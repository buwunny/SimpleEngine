#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp> 
#include "Window.hpp"

class Camera {
public:
    Camera(Window& window);
    ~Camera();

    void update(const glm::vec3& position, const glm::vec3& front, const glm::vec3& up);
    void rotate(float yaw, float pitch);

    glm::vec3 getPosition() const { return position; }
    glm::vec3 getFront() const { return front; }
    glm::vec3 getUp() const { return up; }
    glm::vec3 getRight() const { return right; }
    glm::mat4 getViewMatrix() const { return viewMatrix; }
    glm::mat4 getProjectionMatrix() const { return projectionMatrix; }

private:
    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
    float yaw;
    float pitch;
};
#endif // CAMERA_HPP