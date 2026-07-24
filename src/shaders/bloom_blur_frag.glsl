#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uTex;
uniform vec2 uTexelStep;   // (1/w, 0) for horizontal pass, (0, 1/h) for vertical

// Separable Gaussian, sigma ~2.0, using *linear-sampled* taps: each fetch sits
// between two texels at the weighted midpoint of the pair, so the GPU's
// bilinear filter sums them for free. That covers the same 9-texel kernel this
// replaced in 5 fetches instead of 9, exactly rather than approximately — the
// offsets and weights below are derived from its weights.
//
// Worth doing here above anywhere else: this shader runs twice per bloom
// iteration across the whole bloom buffer, so it is the hottest pass in the
// frame and is pure texture bandwidth, which is exactly what low-end and mobile
// GPUs are shortest of.
//
// Derived from {w0..w4} = {0.227027, 0.194595, 0.121622, 0.054054, 0.016216}
// by pairing taps (1,2) and (3,4).
const float kWeightCenter = 0.227027;
const float kWeightInner = 0.316217;  // w1 + w2
const float kWeightOuter = 0.070270;  // w3 + w4
const float kOffsetInner = 1.384615;  // (1*w1 + 2*w2) / (w1 + w2)
const float kOffsetOuter = 3.230769;  // (3*w3 + 4*w4) / (w3 + w4)

void main()
{
    vec3 sum = texture(uTex, vUV).rgb * kWeightCenter;

    vec2 inner = uTexelStep * kOffsetInner;
    sum += texture(uTex, vUV + inner).rgb * kWeightInner;
    sum += texture(uTex, vUV - inner).rgb * kWeightInner;

#ifdef QUALITY_LOW
    // The outer pair carries 7% of the kernel. Dropping it on Low saves two
    // more fetches for a marginally narrower blur; the remaining weights are
    // renormalised so overall brightness is unchanged.
    sum *= 1.0 / (kWeightCenter + 2.0 * kWeightInner);
#else
    vec2 outer = uTexelStep * kOffsetOuter;
    sum += texture(uTex, vUV + outer).rgb * kWeightOuter;
    sum += texture(uTex, vUV - outer).rgb * kWeightOuter;
#endif

    FragColor = vec4(sum, 1.0);
}
