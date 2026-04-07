#version 450

layout(push_constant) uniform PC {
    mat4  viewProj;
    vec4  sphereWorldPos; // xyz=pos, w=innerRadius
    vec4  color;          // rgb=core, a=unused
    vec4  glowColor;      // rgb=glow, a=age
    vec4  camPos;
    vec4  params;         // x=element, y=sunIntensity, z=outerRadius, w=unused
} pc;

layout(location = 0) out vec3 fragRayOrig;
layout(location = 1) out vec3 fragRayDir;
layout(location = 2) out vec2 fragUV;

void main() {
    // Fullscreen triangle
    vec2 pos;
    if      (gl_VertexIndex == 0) pos = vec2(-1.0, -1.0);
    else if (gl_VertexIndex == 1) pos = vec2( 3.0, -1.0);
    else                          pos = vec2(-1.0,  3.0);

    fragUV = pos * 0.5 + 0.5;

    // Reconstruct ray from camera through this pixel toward sphere
    // We'll clip the quad to a billboard around the sphere in clip space
    vec3 spherePos = pc.sphereWorldPos.xyz;
    float outerR   = pc.params.z;

    // Billboard: find sphere center in clip space, expand by outerR
    vec4 clip = pc.viewProj * vec4(spherePos, 1.0);
    vec2 ndcCenter = clip.xy / clip.w;
    float depth    = clip.z / clip.w;

    // Scale billboard to cover sphere in screen space (conservative)
    float screenScale = outerR / max(clip.w * 0.01, 0.001);
    vec2 quadPos = ndcCenter + pos * screenScale * 1.5;

    gl_Position = vec4(quadPos, depth, 1.0);

    // Ray origin = camera position
    fragRayOrig = pc.camPos.xyz;

    // Ray direction through this NDC position
    // Unproject: use inverse of viewProj
    // Approximate: use the quad position as NDC
    vec4 worldFar = (pc.viewProj) * vec4(quadPos, 1.0, 1.0);
    worldFar.xyz /= worldFar.w;
    fragRayDir = normalize(worldFar.xyz - pc.camPos.xyz);
}
