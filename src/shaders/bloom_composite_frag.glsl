#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uBloomIntensity;
uniform int uTonemap;       // 1 = apply Reinhard + gamma (only meaningful when bloom is on)
uniform int uScanlines;
uniform float uScanlineStrength;
uniform float uTime;
uniform vec2 uOutputSize;

vec3 tonemap(vec3 x)
{
    return x / (x + vec3(1.0));
}

void main()
{
    vec3 scene = texture(uScene, vUV).rgb;
    vec3 bloom = texture(uBloom, vUV).rgb;
    vec3 col = scene + bloom * uBloomIntensity;

    if (uTonemap == 1)
    {
        col = tonemap(col);
#ifdef QUALITY_LOW
        // sqrt is gamma 2.0 rather than 2.2 — a shade brighter in the mids and
        // visually very close, but a single instruction against a full pow().
        col = sqrt(col);
#else
        col = pow(col, vec3(1.0 / 2.2));
#endif
    }

    if (uScanlines == 1)
    {
#ifdef QUALITY_LOW
        // A triangle wave off fract() instead of sin(). At one cycle per pixel
        // row the difference is imperceptible, and it drops a transcendental
        // from a fullscreen pass.
        float line = abs(fract(vUV.y * uOutputSize.y * 0.5) * 2.0 - 1.0) * 2.0 - 1.0;
#else
        float line = sin(vUV.y * uOutputSize.y * 3.14159);
#endif
        float dark = mix(1.0, 0.5 + 0.5 * line, uScanlineStrength);
        col *= dark;
    }

    FragColor = vec4(col, 1.0);
}
