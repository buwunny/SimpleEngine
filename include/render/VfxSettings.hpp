#ifndef VFX_SETTINGS_HPP
#define VFX_SETTINGS_HPP

#include <glm/glm.hpp>

// Vaporwave / VFX settings. Read by Application each frame to drive the sky-grid
// background pass, the main shader's fog + neon intensity, and the bloom
// post-process chain.
//
// This is a plain-data struct with no ImGui dependency on purpose: the runtime
// (PostFX, Application's standalone game loop) needs it, so it must be
// includable by targets that do not link the editor UI. The editor's Context
// aliases it as editor::Context::VFX.
namespace editor
{
    // How much the renderer is allowed to spend per pixel. This is not a
    // cosmetic switch: PostFX compiles a different variant of every
    // post-process shader per level and sizes the bloom chain from it, so
    // changing it recompiles. Low exists for phones and integrated GPUs, where
    // the fullscreen passes are fill-rate bound and the difference is the gap
    // between 30 and 60 fps.
    enum class Quality
    {
        Low = 0,
        Medium = 1,
        High = 2,
    };

    // Most blast lights that can affect a pixel at once. Kept small and fixed
    // because it sets the loop bound in two fragment shaders; anything past
    // this many simultaneous explosions simply isn't lit, which is invisible in
    // practice — a fifth blast within range of the same pixel is already lost
    // in the glare of the other four.
    inline constexpr int kMaxBlastLights = 4;

    // The shaders declare their uniform arrays with a literal, since GLSL can't
    // see this header. If you raise the limit here, raise MAX_BLAST_LIGHTS in
    // both src/shaders/fragment.glsl and src/shaders/sky_vaporwave_frag.glsl to
    // match — otherwise the extra lights are uploaded and silently ignored.
    static_assert(kMaxBlastLights == 4,
                  "MAX_BLAST_LIGHTS in fragment.glsl and sky_vaporwave_frag.glsl must match");

    struct VFX
    {
        // Every visual feature is gated by its own *Enabled flag so the
        // editor boots into a plain wireframe-on-dark look. Users opt in
        // to each effect via the VFX panel.

        // Sky gradient
        bool skyEnabled = false;
        glm::vec3 skyTop = glm::vec3(0.05f, 0.02f, 0.18f);     // deep purple
        glm::vec3 skyMid = glm::vec3(0.85f, 0.18f, 0.55f);     // hot pink
        glm::vec3 skyBottom = glm::vec3(1.00f, 0.55f, 0.20f);  // sunset orange

        // Sun disk + glow
        bool sunEnabled = false;
        // sunPos is used only in screen-anchored mode (NDC: -1..1 on both axes).
        glm::vec2 sunPos = glm::vec2(0.0f, 0.18f);
        float sunRadius = 0.22f;
        glm::vec3 sunColor = glm::vec3(1.0f, 0.85f, 0.45f);
        int sunStripes = 6;
        // When true, the sun is fixed at a world-space direction (set by
        // azimuth + elevation) and projected to screen each frame so it
        // sits on the horizon rather than drifting with the camera.
        bool sunWorldAnchored = true;
        float sunAzimuth = 0.0f;    // degrees around Y, 0 = looking down -Z
        float sunElevation = 6.0f;  // degrees above the horizon

        // Perspective grid floor
        bool gridEnabled = false;
        glm::vec3 gridColor = glm::vec3(1.0f, 0.25f, 0.85f);   // neon magenta
        float gridScale = 4.0f;                                // world units between major lines
        float gridFade = 120.0f;                               // distance at which grid fades to 0
        float gridLineWidth = 0.04f;                           // line thickness (0..1)
        float horizonY = 0.0f;                                 // world-Y of the grid plane

        // Solid black fill behind each wireframe object (legibility — hides sky)
        bool wireframeFill = true;

        // Distance fog applied to the wireframe lines
        bool fogEnabled = false;
        glm::vec3 fogColor = glm::vec3(0.30f, 0.05f, 0.30f);
        float fogStart = 12.0f;
        float fogEnd = 140.0f;

        // Neon brightness boost on wireframe colors (1.0 = pass-through)
        bool neonEnabled = false;
        float neonIntensity = 1.0f;

        // Bloom post-process (also enables HDR tonemap + gamma so neon stays balanced)
        bool bloomEnabled = false;
        float bloomThreshold = 0.55f;
        float bloomIntensity = 1.4f;
        float bloomRadius = 1.0f;                              // blur kernel scale
        int bloomIterations = 5;                               // ping-pong passes per axis pair

        // Retro CRT overlay
        bool scanlinesEnabled = false;
        float scanlineStrength = 0.15f;

        // Explosions cast light. Each live blast becomes a point light that
        // brightens nearby wireframe lines and the grid floor beneath it, on
        // the same fade curve as the shockwave ring that draws it (see
        // ecs::BlastVfx). Costs a short bounded loop per pixel in two shaders
        // and nothing at all while no blast is alive.
        bool explosionLightEnabled = false;
        // Multiplies a lit line's own colour, so this is a gain and not an
        // absolute brightness: past ~2 everything inside the radius clips to
        // white and the blast stops reading as a light and starts reading as a
        // flash of nothing.
        float explosionLightIntensity = 1.5f;
        // Multiplies the blast's own radius to get the light's reach. Light
        // spills further than the shockwave that produced it, or the effect
        // reads as the ring being the only thing that happened.
        float explosionLightReach = 2.0f;
        glm::vec3 explosionLightColor = glm::vec3(1.0f, 0.62f, 0.22f);

        Quality quality = Quality::High;
    };
}

#endif // VFX_SETTINGS_HPP
