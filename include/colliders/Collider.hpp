#ifndef COLLIDER_HPP
#define COLLIDER_HPP

#include <glm/glm.hpp>

class Collider {
public:
    virtual ~Collider() = default;
    virtual bool checkCollision(const Collider& other) const = 0;
    virtual glm::vec3 getPosition() const = 0;
    virtual void setPosition(const glm::vec3& position) = 0;
};

#endif // COLLIDER_HPP