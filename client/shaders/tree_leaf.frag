#version 450

layout(location = 0) in vec2  fragUV;
layout(location = 1) in float fragNoise;
layout(location = 2) in float fragAlpha;

layout(push_constant) uniform PC {
    mat4  viewProj;
    vec4  params;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    // Discard nearly transparent pixels
    if (fragAlpha < 0.01) discard;

    // Leaf color — two greens blended by noise
    vec3 leafDark  = vec3(0.08, 0.22, 0.06);
    vec3 leafLight = vec3(0.18, 0.42, 0.10);
    // Slight yellow-green on bright spots
    vec3 leafBright = vec3(0.28, 0.52, 0.08);

    float n = clamp(fragNoise, 0.0, 1.0);
    vec3 leafCol;
    if (n > 0.6)
        leafCol = mix(leafLight, leafBright, (n - 0.6) / 0.4);
    else
        leafCol = mix(leafDark, leafLight, n / 0.6);

    // Simple top-down lighting — underside darker
    float light = 0.4 + 0.6 * fragAlpha; // brighter toward center of quad

    outColor = vec4(leafCol * light, fragAlpha * 0.92);
}
