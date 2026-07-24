#ifndef ECS_BLAST_VFX_SYSTEM_HPP
#define ECS_BLAST_VFX_SYSTEM_HPP

#include "ecs/Entity.hpp"

#include <glm/glm.hpp>

namespace ecs
{
    // Advance every BlastVfx ring: grow it toward its blast radius, fade it out,
    // and destroy the entity once its life is spent. Call once per rendered
    // frame with the frame delta — this is animation, not simulation, so it runs
    // on real frame time and never on the headless server.
    void blastVfxSystem(Registry &r, float dt);

    // Gather the live blasts as point lights for the scene and sky shaders.
    // Writes at most `maxLights` entries and returns how many:
    //   posRadius[i]      = (world position, reach in metres)
    //   colorIntensity[i] = (light colour, brightness)
    //
    // When more blasts are alive than there are slots, the brightest win —
    // dropping a blast that has nearly faded is far less visible than dropping
    // one that just went off. `reach` scales the blast's own radius and
    // `intensity` its peak brightness; both come from the VFX settings.
    int collectBlastLights(Registry &r, glm::vec4 *posRadius, glm::vec4 *colorIntensity,
                           int maxLights, float reach, float intensity);
}

#endif // ECS_BLAST_VFX_SYSTEM_HPP
