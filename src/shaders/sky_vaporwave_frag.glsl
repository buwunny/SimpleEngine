#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform mat4 uInvViewProj;
uniform vec3 uCamPos;

uniform vec3 uSkyTop;
uniform vec3 uSkyMid;
uniform vec3 uSkyBottom;

uniform vec2 uSunPos;       // world-anchored mode: NDC (x, y in [-1,1])
                            // screen-anchored mode: (x in [-1,1], height above horizon)
uniform int uSunWorldAnchored;
uniform int uSunVisible;
uniform float uSunRadius;
uniform vec3 uSunColor;
uniform int uSunStripes;
uniform int uSkyEnabled;

uniform float uAspect;      // width / height — keeps the sun disk circular

uniform int uGridEnabled;
uniform vec3 uGridColor;
uniform float uGridScale;
uniform float uGridFade;
uniform float uGridLineWidth;
uniform float uGridPlaneY;

// --- Blast lights -----------------------------------------------------------
// Kept byte-identical to the same block in fragment.glsl. GLSL has no include
// mechanism here, and the uniform declarations could not be shared even if the
// function could, so the two copies are maintained together on purpose. Change
// one, change the other. MAX_BLAST_LIGHTS must equal editor::kMaxBlastLights;
// VfxSettings.hpp static_asserts that it does.
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

float gridLine(float v, float w)
{
    // Distance from nearest integer line; smoothstep returns 1 on the line, 0 between.
    float d = abs(fract(v - 0.5) - 0.5);
    float aa = fwidth(v) * 1.5;
    return 1.0 - smoothstep(w - aa, w + aa, d);
}

void main()
{
    // Gradient sky uses screen-y so the bands stay fixed on screen.
    float t = clamp(vUV.y, 0.0, 1.0);
    vec3 lower = mix(uSkyBottom, uSkyMid, smoothstep(0.0, 0.55, t));
    vec3 sky   = mix(lower,      uSkyTop, smoothstep(0.45, 1.0, t));

    if (uSkyEnabled == 0)
        sky = vec3(0.02, 0.02, 0.04);

    vec3 col = sky;

    // Glowing striped sun. World-anchored mode receives the sun's projected
    // NDC position; screen-anchored mode uses sunPos.y as "height above the
    // (vUV.y = 0.5) horizon line", scaled to half-screen.
    //
    // All of this sits inside the visibility test. It used to run on every
    // pixel of every frame and only be *discarded* if the sun was off, which
    // meant a transcendental and a stripe evaluation per pixel for a feature
    // that ships disabled by default.
    if (uSunVisible == 1)
    {
        vec2 sunCenter;
        if (uSunWorldAnchored == 1)
            sunCenter = uSunPos * 0.5 + 0.5;
        else
            sunCenter = vec2(uSunPos.x * 0.5 + 0.5, 0.5 + uSunPos.y * 0.5);
        vec2 sunUV = vUV - sunCenter;
        // Multiply x by aspect (width/height) so equal sunUV distances map to
        // equal pixel distances — without this, the disk stretches horizontally
        // on a wide viewport.
        sunUV.x *= uAspect;
        float invRadius = 1.0 / max(uSunRadius, 1e-4);
        float sunDist = length(sunUV);
        float sunDisk = smoothstep(uSunRadius, uSunRadius * 0.85, sunDist);

        // Glow falloff. pow() with a fractional exponent is one of the most
        // expensive things a fragment shader can do; exp2 of a squared distance
        // gives an almost identical curve for a fraction of the cost.
        float glowT = sunDist * invRadius;
        float sunGlow = exp2(-glowT * glowT * 2.885);

        // Horizontal stripes cut out the lower half of the disk
        if (uSunStripes > 0)
        {
            float relY = (sunUV.y * invRadius) * 0.5 + 0.5;  // 0..1 within disk
            // Stripes grow thicker toward the bottom of the sun
            float bandT = 1.0 - relY;
            float stripeMask = step(0.0, sin(bandT * 3.14159 * float(uSunStripes) * (0.5 + bandT)));
            // Only cut stripes in lower half of the sun
            float lowerHalf = smoothstep(0.55, 0.45, relY);
            sunDisk *= mix(1.0, stripeMask, lowerHalf);
        }

        col += uSunColor * (sunDisk + sunGlow * 0.45);
    }

    // Perspective grid floor — intersect a per-pixel world ray with y = uGridPlaneY.
    //
    // The ray reconstruction lives in here rather than at the top of main()
    // because it is two mat4 transforms, two divides and a normalize per pixel,
    // and the grid is the only thing that ever consumes it. Hoisted out, a
    // grid-less frame paid for all of it and threw the result away.
    if (uGridEnabled == 1)
    {
        vec2 ndc = vUV * 2.0 - 1.0;
        vec4 nearH = uInvViewProj * vec4(ndc, -1.0, 1.0);
        vec4 farH  = uInvViewProj * vec4(ndc,  1.0, 1.0);
        vec3 nearW = nearH.xyz / nearH.w;
        vec3 farW  = farH.xyz  / farH.w;
        vec3 rayDir = normalize(farW - nearW);

        float denom = rayDir.y;
        // Only draw where the ray points down toward the plane below the camera
        if (abs(denom) > 1e-4)
        {
            float tHit = (uGridPlaneY - uCamPos.y) / denom;
            if (tHit > 0.0)
            {
                vec3 hit = uCamPos + rayDir * tHit;
                vec2 g = hit.xz / max(uGridScale, 1e-3);
                float lx = gridLine(g.x, uGridLineWidth);
                float lz = gridLine(g.y, uGridLineWidth);
                float lineMask = max(lx, lz);

                // Distance fade
                float dist = length(hit.xz - uCamPos.xz);
                float fade = 1.0 - smoothstep(uGridFade * 0.2, uGridFade, dist);
                // Horizon haze — soften lines near the vanishing line
                float horizonSoft = smoothstep(0.0, 0.15, abs(denom));

                vec3 gridCol = uGridColor * lineMask * fade * horizonSoft;

                // Explosions light the floor they go off above. This is what
                // sells a blast as a light source rather than a decal: the grid
                // is the largest surface on screen, so a pool of light sweeping
                // across it reads instantly, even for a blast off-camera.
                if (uBlastLightCount > 0)
                {
                    vec3 lit = vec3(0.0);
                    for (int i = 0; i < MAX_BLAST_LIGHTS; ++i)
                    {
                        if (i >= uBlastLightCount)
                            break;
                        lit += blastLight(hit, i);
                    }
                    // Applied to the grid lines rather than the whole plane:
                    // the space between lines is unlit sky, and brightening it
                    // would turn the floor into a solid sheet.
                    gridCol += uGridColor * lineMask * fade * horizonSoft * lit;
                }

                // Boost intensity so bloom catches the brightest lines
                col += gridCol * 1.6;
            }
        }
    }

    FragColor = vec4(col, 1.0);
}
