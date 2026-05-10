#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    vec3  sunDir  = normalize(vec3(0.6, 1.0, 0.4));
    float diffuse = max(dot(normalize(fragNormal), sunDir), 0.0);
    float light   = clamp(0.15 + diffuse * 0.85, 0.0, 1.0);
    outColor = vec4(fragColor.rgb * light, 1.0);
}
