#version 450

layout(push_constant) uniform PC {
    mat4  viewProj;
    vec4  sphereWorldPos;
    vec4  color;
    vec4  glowColor;
    vec4  camPos;
    vec4  params;
} pc;

layout(location = 0) out vec3 fragRayOrig;
layout(location = 1) out vec3 fragRayDir;
layout(location = 2) out vec2 fragUV;

void main() {
    vec3 spherePos = pc.sphereWorldPos.xyz;
    float outerR   = pc.params.z;

    // Project sphere center to clip space
    vec4 clip = pc.viewProj * vec4(spherePos, 1.0);

    // Billboard quad corners in NDC, expanded to cover the sphere
    vec2 corners[4];
    corners[0] = vec2(-1.0, -1.0);
    corners[1] = vec2( 1.0, -1.0);
    corners[2] = vec2(-1.0,  1.0);
    corners[3] = vec2( 1.0,  1.0);

    // Triangle list: verts 0,1,2 and 0,2,3 — use index 0,1,2,0,2,3
    int idx[6] = int[](0,1,2,0,2,3);
    vec2 corner = corners[idx[gl_VertexIndex]];

    // Scale billboard: project a point outerR to the side of the sphere
    // to get the screen-space half-size
    vec4 edgeClip = pc.viewProj * vec4(spherePos + vec3(outerR, 0.0, 0.0), 1.0);
    vec2 ndcCenter = clip.xy / clip.w;
    vec2 ndcEdge   = edgeClip.xy / edgeClip.w;
    float ndcRadius = length(ndcEdge - ndcCenter) * 1.5; // 1.5 = safety margin

    vec2 quadNDC = ndcCenter + corner * ndcRadius;
    gl_Position  = vec4(quadNDC, clip.z / clip.w, 1.0);
    fragUV       = corner * 0.5 + 0.5;

    // Ray from camera through this NDC position
    // Unproject using inverse viewProj
    mat4 invVP = inverse(pc.viewProj);
    vec4 nearW = invVP * vec4(quadNDC, -1.0, 1.0);
    vec4 farW  = invVP * vec4(quadNDC,  1.0, 1.0);
    nearW.xyz /= nearW.w;
    farW.xyz  /= farW.w;

    fragRayOrig = pc.camPos.xyz;
    fragRayDir  = normalize(farW.xyz - nearW.xyz);
}
