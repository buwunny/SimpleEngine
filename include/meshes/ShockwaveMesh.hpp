#ifndef SHOCKWAVE_MESH_HPP
#define SHOCKWAVE_MESH_HPP

#include "Mesh.hpp"

// The expanding blast sphere an explosion leaves behind: three orthogonal
// unit-radius rings (XZ, XY, YZ) in one buffer, drawn as GL_LINES.
//
// Rings rather than a shaded sphere because this renderer draws the world as
// wireframe — a solid blast would be the only opaque object on screen. Unit
// radius because the explosion animates its size through the entity Transform,
// so the mesh itself is built once and shared by every blast.
class ShockwaveMesh : public Mesh
{
public:
    ShockwaveMesh();

    // Deliberately empty. renderSystem's first pass calls render() to lay a
    // black silhouette behind an object so its wireframe stays legible against
    // the sky; doing that for a shockwave would punch a hole in whatever the
    // blast is expanding over.
    void render() override {}

    // The base implementation derives edges by reading `indices` three at a
    // time as triangles. These indices are already line pairs, so it draws them
    // directly instead.
    void renderWireframe() override;
};

#endif // SHOCKWAVE_MESH_HPP
