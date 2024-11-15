#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera {
public:
    Camera(glm::vec3 position, glm::vec3 front, glm::vec3 up);
    ~Camera();

    glm::vec3 getPosition() { return position; };
    glm::vec3 getFront() { return front; };
    glm::vec3 getUp() { return up; };
    float getYaw() { return yaw; };
    float getPitch() { return pitch; };

    void setPosition(glm::vec3 position) { this->position = position; };
    void setFront(glm::vec3 front) { this->front = front; };
    void setUp(glm::vec3 up) { this->up = up; };

    void look(float xoffset, float yoffset);
private:
    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    float yaw;
    float pitch;
    float sensitivity = 0.1f;

};
#endif // CAMERA_HPP