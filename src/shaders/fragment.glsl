#version 330 core
in vec3 vWorldPos;
out vec4 FragColor;

uniform vec4 wireframeColor;
uniform vec3 uCamPos;

uniform int uFogEnabled;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;

uniform float uNeonIntensity;

// --- Blast lights -----------------------------------------------------------
// Kept byte-identical to the same block in sky_vaporwave_frag.glsl. GLSL has no
// include mechanism here, and the uniform declarations could not be shared even
// if the function could, so the two copies are maintained together on purpose.
// Change one, change the other. MAX_BLAST_LIGHTS must equal
// editor::kMaxBlastLights; VfxSettings.hpp static_asserts that it does.
#define MAX_BLAST_LIGHTS 4
uniform int uBlastLightCount;
uniform vec4 uBlastLightPos[MAX_BLAST_LIGHTS];    // xyz = world position, w = radius
uniform vec4 uBlastLightColor[MAX_BLAST_LIGHTS];  // rgb = colour, a = intensity

vec3 blastLight(vec3 p, int i)
{
    vec3 d = p - uBlastLightPos[i].xyz;
    float r = max(uBlastLightPos[i].w, 1e-3);
    // Inverse-square would need a clamp at the origin and a cutoff at the far
    // end anyway; this falls to exactly zero at the radius, which keeps the
    // light local and lets the loop stay branch-free.
    float t = clamp(1.0 - dot(d, d) / (r * r), 0.0, 1.0);
    return uBlastLightColor[i].rgb * (t * t * uBlastLightColor[i].a);
}
// --- end blast lights -------------------------------------------------------

void main()
{
    vec3 base = wireframeColor.rgb * uNeonIntensity;
    if (uFogEnabled == 1)
    {
        float dist = distance(vWorldPos, uCamPos);
        float f = clamp((dist - uFogStart) / max(uFogEnd - uFogStart, 1e-3), 0.0, 1.0);
        // Fade toward fog color but keep some neon to still register in bloom.
        base = mix(base, uFogColor, f * 0.85);
    }

    // Explosions light the geometry around them. Applied after fog so a blast
    // still punches through haze — a light source fog could swallow reads as a
    // texture rather than something emitting.
    //
    // Purely multiplicative, which matters more here than in a shaded renderer.
    // renderSystem lays a *black* fill behind every wireframe for legibility,
    // and that fill comes through this same shader; any flat additive term
    // lights it too, turning each object and the ground plane into a solid
    // glowing slab and erasing the wireframe look entirely. Multiplying leaves
    // black at black and brightens only what already has colour — the lines.
    if (uBlastLightCount > 0)
    {
        vec3 lit = vec3(0.0);
        for (int i = 0; i < MAX_BLAST_LIGHTS; ++i)
        {
            if (i >= uBlastLightCount)
                break;
            lit += blastLight(vWorldPos, i);
        }
        base += base * lit;
    }

    FragColor = vec4(base, wireframeColor.a);
}
