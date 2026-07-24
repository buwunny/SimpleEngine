#include "meshes/ShockwaveMesh.hpp"

#include <cmath>

namespace
{
    // Enough segments that the ring still reads as a circle at the radius a
    // blast reaches, but the whole mesh is one draw call either way.
    constexpr int kSegments = 48;
    constexpr float kTwoPi = 6.28318530718f;
}

ShockwaveMesh::ShockwaveMesh()
{
    vertices.reserve(3 * kSegments * 3);
    indices.reserve(3 * kSegments * 2);

    for (int plane = 0; plane < 3; ++plane)
    {
        const unsigned int base = static_cast<unsigned int>(plane * kSegments);
        for (int i = 0; i < kSegments; ++i)
        {
            const float a = kTwoPi * static_cast<float>(i) / static_cast<float>(kSegments);
            const float c = std::cos(a);
            const float s = std::sin(a);

            if (plane == 0) // horizontal — the one that reads as a ground ring
            {
                vertices.insert(vertices.end(), {c, 0.0f, s});
            }
            else if (plane == 1)
            {
                vertices.insert(vertices.end(), {c, s, 0.0f});
            }
            else
            {
                vertices.insert(vertices.end(), {0.0f, c, s});
            }

            // Line pair to the next point, wrapping to close the ring.
            indices.push_back(base + static_cast<unsigned int>(i));
            indices.push_back(base + static_cast<unsigned int>((i + 1) % kSegments));
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

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
}

void ShockwaveMesh::renderWireframe()
{
    glBindVertexArray(VAO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glDrawElements(GL_LINES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}
