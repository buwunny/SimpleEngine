#include "../../include/colliders/CubeCollider.hpp"

bool CubeCollider::checkCollision(const Collider& other) const {
    const CubeCollider* otherCube = dynamic_cast<const CubeCollider*>(&other);
    if (otherCube) {
        glm::vec3 halfSize = dimensions / 2.0f;
        glm::vec3 otherHalfSize = otherCube->getDimensions() / 2.0f;
        glm::vec3 otherPosition = otherCube->getPosition();
        
        return (abs(position.x - otherPosition.x) <= (halfSize.x + otherHalfSize.x)) &&
               (abs(position.y - otherPosition.y) <= (halfSize.y + otherHalfSize.y)) &&
               (abs(position.z - otherPosition.z) <= (halfSize.z + otherHalfSize.z));
    }
    return false;
}