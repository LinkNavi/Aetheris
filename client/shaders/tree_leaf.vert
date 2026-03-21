#version 450

layout(location = 0) in vec3  inPos;
layout(location = 1) in vec2  inUV;
layout(location = 2) in float inNoise;
layout(location = 3) in float inFlutter;

// Per-instance
layout(location = 4) in vec3  instPos;
layout(location = 5) in float instYaw;
layout(location = 6) in float instScale;
layout(location = 7) in float instSeed;

layout(push_constant) uniform PC {
    mat4  viewProj;
    vec4  params; // x = windTime
} pc;

layout(location = 0) out vec2  fragUV;
layout(location = 1) out float fragNoise;
layout(location = 2) out float fragAlpha;

void main() {

    float cy = cos(instYaw), sy = sin(instYaw);
    mat3 rot = mat3(
         cy, 0.0,  sy,
        0.0, 1.0, 0.0,
        -sy, 0.0,  cy
    );

    vec3 localPos = rot * (inPos * instScale);
    vec3 worldPos = instPos + localPos;

    // Wind sway — gentle sinusoidal movement
    float wind     = pc.params.x;
    float heightFac = (inPos.y * instScale) / 8.0; // upper leaves sway more
    heightFac       = clamp(heightFac, 0.0, 1.0);

    float swayX = sin(wind * 1.3 + instSeed * 6.28 + inFlutter) * 0.06 * heightFac;
    float swayZ = cos(wind * 0.9 + instSeed * 4.71 + inFlutter * 0.7) * 0.04 * heightFac;

    // Per-leaf flutter — small high-freq wobble
    float flutter = sin(wind * 4.0 + inFlutter) * 0.015 * heightFac;

    worldPos.x += swayX + flutter;
    worldPos.z += swayZ;
    worldPos.y += sin(wind * 2.1 + inFlutter * 1.3) * 0.01 * heightFac;

    gl_Position = pc.viewProj * vec4(worldPos, 1.0);
    fragUV      = inUV;
    fragNoise   = inNoise;

    // Fade alpha based on UV center distance — soft leaf shape
    vec2 centered = inUV * 2.0 - 1.0;
float dist    = length(centered);
// Hard circle edge with soft falloff
fragAlpha = clamp(1.3 - dist * 0.5, 0.0, 1.0);
}
