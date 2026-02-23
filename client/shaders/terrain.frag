#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDir = normalize(vec3(0.6, 1.0, 0.4));
    float diff    = max(dot(normalize(fragNormal), lightDir), 0.0);
    vec3  color   = vec3(0.3, 0.7, 0.3) * (0.2 + 0.8 * diff);
    outColor = vec4(color, 1.0);
}
