#ifndef ECS_BLAST_VFX_SYSTEM_HPP
#define ECS_BLAST_VFX_SYSTEM_HPP

#include "ecs/Entity.hpp"

namespace ecs
{
    // Advance every BlastVfx ring: grow it toward its blast radius, fade it out,
    // and destroy the entity once its life is spent. Call once per rendered
    // frame with the frame delta — this is animation, not simulation, so it runs
    // on real frame time and never on the headless server.
    void blastVfxSystem(Registry &r, float dt);
}

#endif // ECS_BLAST_VFX_SYSTEM_HPP
