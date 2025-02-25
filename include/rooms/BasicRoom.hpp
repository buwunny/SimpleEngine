#ifndef BASICROOM_HPP
#define BASICROOM_HPP

#include "Room.hpp"
#include "../objects/Plane.hpp"
#include "../objects/Cube.hpp"

class BasicRoom : public Room {
public:
    BasicRoom(glm::vec3 position, glm::vec3 front, glm::vec3 up);
    ~BasicRoom(); 
};

#endif // BASICROOM_HPP