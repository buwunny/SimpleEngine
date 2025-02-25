#include "../../include/meshes/PlaneMesh.hpp"

PlaneMesh::PlaneMesh(float x, float z, int xSubdivisions, int zSubdivisions) {
    float halfX = x / 2.0f;
    float halfZ = z / 2.0f;
    float stepX = x / static_cast<float>(xSubdivisions);
    float stepZ = z / static_cast<float>(zSubdivisions);

    for (int i = 0; i <= zSubdivisions; ++i) {
        for (int j = 0; j <= xSubdivisions; ++j) {
            float posX = -halfX + j * stepX;
            float posZ = -halfZ + i * stepZ;
            vertices.push_back(posX);
            vertices.push_back(0.0f);
            vertices.push_back(posZ);
        }
    }

    for (int i = 0; i < zSubdivisions; ++i) {
        for (int j = 0; j < xSubdivisions; ++j) {
            int topLeft = i * (xSubdivisions + 1) + j;
            int topRight = topLeft + 1;
            int bottomLeft = (i + 1) * (xSubdivisions + 1) + j;
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

PlaneMesh::~PlaneMesh() {
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
}

void PlaneMesh::render() {
    glBindVertexArray(VAO);
    glDrawElements(GL_QUADS, indices.size(), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}
