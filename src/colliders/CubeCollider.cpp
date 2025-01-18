#include "../../include/colliders/CubeCollider.hpp"

CubeCollider::CubeCollider(glm::vec3 position, float size)
    : position(position), size(size) {}

bool CubeCollider::checkCollision(const Collider& other) const {
    const CubeCollider* otherCube = dynamic_cast<const CubeCollider*>(&other);
    if (otherCube) {
        float halfSize = size / 2.0f;
        float otherHalfSize = otherCube->getSize() / 2.0f;
        return (abs(position.x - otherCube->getPosition().x) <= (halfSize + otherHalfSize)) &&
               (abs(position.y - otherCube->getPosition().y) <= (halfSize + otherHalfSize)) &&
               (abs(position.z - otherCube->getPosition().z) <= (halfSize + otherHalfSize));
    }
    return false;
}