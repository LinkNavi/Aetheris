#version 450

const vec3 positions[8] = vec3[](
    vec3(-0.5, -0.5, -0.5),
    vec3( 0.5, -0.5, -0.5),
    vec3( 0.5,  0.5, -0.5),
    vec3(-0.5,  0.5, -0.5),
    vec3(-0.5, -0.5,  0.5),
    vec3( 0.5, -0.5,  0.5),
    vec3( 0.5,  0.5,  0.5),
    vec3(-0.5,  0.5,  0.5)
);

const int indices[36] = int[](
    0,1,2, 2,3,0,
    4,5,6, 6,7,4,
    0,4,7, 7,3,0,
    1,5,6, 6,2,1,
    3,2,6, 6,7,3,
    0,1,5, 5,4,0
);

layout(push_constant) uniform PC {
    mat4 mvp;
} pc;

layout(location = 0) out vec3 fragColor;

void main() {
    vec3 pos = positions[indices[gl_VertexIndex]];
    gl_Position = pc.mvp * vec4(pos, 1.0);
    fragColor = pos + 0.5;
}
