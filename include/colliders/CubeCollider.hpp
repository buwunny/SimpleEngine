#ifndef CUBE_COLLIDER_HPP
#define CUBE_COLLIDER_HPP

#include "Collider.hpp"

class CubeCollider : public Collider {
public:
    CubeCollider(glm::vec3 position, float size) : position(position), dimensions(size) {};
    CubeCollider(glm::vec3 position, glm::vec3 dimensions) : position(position), dimensions(dimensions) {};
    ~CubeCollider() override = default;

    bool checkCollision(const Collider& other) const override;
    glm::vec3 getPosition() const override { return position; }
    void setPosition(const glm::vec3& position) override { this->position = position; }

    // float getSize() const { return size; }
    glm::vec3 getDimensions() const { return dimensions; }
private:
    glm::vec3 position;
    // float size;
    glm::vec3 dimensions;
};

#endif // CUBE_COLLIDER_HPP