#include "ecs/systems/BlastVfxSystem.hpp"
#include "ecs/Components.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <vector>

namespace ecs
{
    void blastVfxSystem(Registry &r, float dt)
    {
        // Collected rather than destroyed in place: destroying an entity while
        // iterating the view it belongs to invalidates the iteration.
        std::vector<Entity> expired;

        auto view = r.view<Transform, Renderable, BlastVfx>();
        for (auto e : view)
        {
            auto &vfx = view.get<BlastVfx>(e);
            vfx.age += dt;
            if (vfx.age >= vfx.life)
            {
                expired.push_back(e);
                continue;
            }

            const float t = vfx.age / vfx.life;

            // Ease out hard: an explosion's shockwave is nearly at full size
            // almost immediately and then coasts. Growing it linearly reads as a
            // balloon inflating rather than something detonating.
            const float inv = 1.0f - t;
            const float grow = 1.0f - inv * inv * inv;
            const float radius = vfx.radius * grow;

            auto &tr = view.get<Transform>(e);
            tr.scale = glm::dvec3(radius);
            tr.model = tr.modelNoScale * glm::scale(glm::mat4(1.0f), glm::vec3(radius));

            // Start white-hot and cool into the blast colour on the way out,
            // fading the whole thing toward black — the scene is lit as
            // wireframe-on-dark, so dimming the line *is* the fade, and it does
            // not depend on the frame having alpha blending enabled.
            auto &rd = view.get<Renderable>(e);
            const float heat = inv * inv;         // white flash, gone quickly
            const float bright = inv * std::sqrt(inv); // overall fade-out
            rd.color = glm::vec4(glm::mix(glm::vec3(vfx.color), glm::vec3(1.0f), heat) * bright,
                                 vfx.color.a);
            // Thick at the moment of detonation, thinning as the ring stretches.
            rd.lineWidth = 1.0 + 2.0 * static_cast<double>(inv);
        }

        for (Entity e : expired)
            r.destroy(e);
    }
}
