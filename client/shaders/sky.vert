#version 450

layout(location = 0) out vec2 fragNDC;

layout(push_constant) uniform PC {
    mat4  invViewProj;
    vec4  sunDir;
    vec4  params;
    vec4  camPos;
} pc;

void main() {
    vec2 ndc;
    if      (gl_VertexIndex == 0) ndc = vec2(-1.0, -1.0);
    else if (gl_VertexIndex == 1) ndc = vec2( 3.0, -1.0);
    else                          ndc = vec2(-1.0,  3.0);

    fragNDC = ndc;
    gl_Position = vec4(ndc, 0.9999, 1.0);
}
