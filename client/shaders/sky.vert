#version 450

layout(location = 0) out vec3 fragRayDir;

layout(push_constant) uniform PC {
    mat4  invViewRot;  // inverse of rotation-only view (no proj, no translation)
    vec4  sunDir;
    vec4  params;
    vec4  camPos;
} pc;

void main() {
    vec2 ndc;
    if      (gl_VertexIndex == 0) ndc = vec2(-1.0, -1.0);
    else if (gl_VertexIndex == 1) ndc = vec2( 3.0, -1.0);
    else                          ndc = vec2(-1.0,  3.0);

    // invViewRot is inverse(mat4(mat3(view))) — pure rotation, no proj, no translation.
    // Multiplying an NDC direction through it gives a correct world-space ray direction.
    vec4 dir   = pc.invViewRot * vec4(ndc, 0.0, 0.0);
    fragRayDir = normalize(dir.xyz);

    gl_Position = vec4(ndc, 0.9999, 1.0);
}
