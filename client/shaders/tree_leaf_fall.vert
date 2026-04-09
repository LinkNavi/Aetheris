#version 450

layout(location = 0) in vec3  inPos;
layout(location = 1) in vec2  inUV;
layout(location = 2) in float inNoise;
layout(location = 3) in float inFlutter;

layout(push_constant) uniform PC {
    mat4  viewProj;
    mat4  model;
    vec4  params;      // x=windTime, y=sunIntensity, z=fogStart, w=fogEnd
    vec4  camPos;
    vec4  sunDir;
} pc;

layout(location = 0) out vec2  fragUV;
layout(location = 1) out float fragNoise;
layout(location = 2) out float fragAlpha;
layout(location = 3) out float fragDist;

void main() {
    // Apply wind relative to model space, then transform
    float wind      = pc.params.x;
    float heightFac = clamp(inPos.y / 8.0, 0.0, 1.0);
    float swayX     = sin(wind*1.3 + inFlutter)     * 0.06 * heightFac;
    float swayZ     = cos(wind*0.9 + inFlutter*0.7) * 0.04 * heightFac;

    vec3 localPos = inPos + vec3(swayX, 0.0, swayZ);
    vec4 worldPos = pc.model * vec4(localPos, 1.0);

    gl_Position = pc.viewProj * worldPos;
    fragUV      = inUV;
    fragNoise   = inNoise;
    fragDist    = length(worldPos.xz - pc.camPos.xz);

    vec2 centered = inUV * 2.0 - 1.0;
    fragAlpha = clamp(1.3 - length(centered) * 0.5, 0.0, 1.0);
}
