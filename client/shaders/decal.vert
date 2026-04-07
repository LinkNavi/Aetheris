#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;

layout(push_constant) uniform PC {
    mat4  viewProj;
    vec4  color;
    vec4  glowColor;
    vec4  params; // x=age, y=lifetime, z=element, w=complexity (0-1)
    vec4  mana;   // x=manaSpent (for glow), yzw=unused
} pc;

layout(location = 0) out vec2  fragUV;
layout(location = 1) out float fragAge;
layout(location = 2) out float fragComplexity;
layout(location = 3) out float fragGlow;

void main() {
    gl_Position  = pc.viewProj * vec4(inPos, 1.0);
    fragUV       = inUV;
    fragAge      = pc.params.x / max(pc.params.y, 0.001);
    fragComplexity = pc.params.w;
    fragGlow     = clamp(pc.mana.x / 200.0, 0.0, 1.0);
}
