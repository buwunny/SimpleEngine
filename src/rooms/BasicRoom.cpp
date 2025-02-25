#include "../../include/rooms/BasicRoom.hpp"

BasicRoom::BasicRoom(glm::vec3 position, glm::vec3 front, glm::vec3 up) {
    Plane* ground = new Plane(100, 100, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 0.0f);
    Plane* ceiling = new Plane(100, 100, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 50.0f, 0.0f)), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 0.0f);
    Plane* plane1 = new Plane(50, 45, glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(50.0f, 25.0f, 27.5f)), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 0.0f);
    Plane* plane2 = new Plane(50, 45, glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(50.0f, 25.0f, -27.5f)), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 0.0f);
    Plane* plane3 = new Plane(40, 10, glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(50.0f, 30.0f, 0.0f)), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 0.0f);
    Plane* plane4 = new Plane(50, 100, glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(-50.0f, 25.0f, 0.0f)), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 0.0f);
    Plane* plane5 = new Plane(100, 50, glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 25.0f, 50.0f)), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 0.0f);
    Plane* plane6 = new Plane(100, 50, glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 25.0f, -50.0f)), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 0.0f);

    Cube* cube1 = new Cube(3, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 15.0f, 0.0f)), glm::vec4(0.5f, 0.5f, 0.5f, 1.0f), 10.0f);

    objects.push_back(ground);
    objects.push_back(ceiling);
    objects.push_back(plane1);
    objects.push_back(plane2);
    objects.push_back(plane3);
    objects.push_back(plane4);
    objects.push_back(plane5);
    objects.push_back(plane6);
    objects.push_back(cube1);
}

BasicRoom::~BasicRoom() {
    objects.clear();
}