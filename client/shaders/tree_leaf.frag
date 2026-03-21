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
    if (fragAlpha < 0.01) discard;

    // Darker, more muted greens — bleak forest palette
    vec3 leafDark   = vec3(0.04, 0.10, 0.03);
    vec3 leafMid    = vec3(0.07, 0.16, 0.05);
    vec3 leafLight  = vec3(0.10, 0.22, 0.06);

    float n = clamp(fragNoise, 0.0, 1.0);
    vec3 leafCol;
    if (n > 0.6)
        leafCol = mix(leafMid, leafLight, (n - 0.6) / 0.4);
    else
        leafCol = mix(leafDark, leafMid, n / 0.6);

    // Desaturate toward grey-green
    float lum = dot(leafCol, vec3(0.299, 0.587, 0.114));
    leafCol = mix(leafCol, vec3(lum * 0.6), 0.35);

    float light = 0.3 + 0.4 * fragAlpha;

    outColor = vec4(leafCol * light, fragAlpha * 0.88);
}
