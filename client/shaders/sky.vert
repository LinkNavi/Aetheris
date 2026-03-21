#version 450

layout(location = 0) out vec3 fragRayDir;

layout(push_constant) uniform PC {
    mat4  invViewRot;  // now: inverse(proj * mat4(mat3(view)))
    vec4  sunDir;
    vec4  params;
    vec4  camPos;
} pc;

void main() {
    vec2 ndc;
    if      (gl_VertexIndex == 0) ndc = vec2(-1.0, -1.0);
    else if (gl_VertexIndex == 1) ndc = vec2( 3.0, -1.0);
    else                          ndc = vec2(-1.0,  3.0);

    // Reconstruct world-space ray direction through inverse(proj * viewRot)
    vec4 dir   = pc.invViewRot * vec4(ndc, 1.0, 1.0);
    fragRayDir = normalize(dir.xyz / dir.w);

    gl_Position = vec4(ndc, 0.9999, 1.0);
}
