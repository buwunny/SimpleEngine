#include "../../include/meshes/Plane.hpp"

Plane::Plane(int length, int width, int subdivisions) {
    float halfLength = length / 2.0f;
    float halfWidth = width / 2.0f;
    float stepLength = length / static_cast<float>(subdivisions);
    float stepWidth = width / static_cast<float>(subdivisions);

    for (int i = 0; i <= subdivisions; ++i) {
        for (int j = 0; j <= subdivisions; ++j) {
            float x = -halfLength + j * stepWidth;
            float z = -halfWidth + i * stepLength;
            vertices.push_back(x);
            vertices.push_back(0.0f);
            vertices.push_back(z);
        }
    }

    for (int i = 0; i < subdivisions; ++i) {
        for (int j = 0; j < subdivisions; ++j) {
            int topLeft = i * (subdivisions + 1) + j;
            int topRight = topLeft + 1;
            int bottomLeft = (i + 1) * (subdivisions + 1) + j;
            int bottomRight = bottomLeft + 1;

            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
            indices.push_back(topRight);
        }
    }

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
}

Plane::~Plane() {
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
}

void Plane::render() {
    glBindVertexArray(VAO);
    glDrawElements(GL_QUADS, indices.size(), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}
