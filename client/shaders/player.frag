#version 450

layout(set = 0, binding = 0) uniform sampler2D atlas;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

void main() {
    vec3  sunDir  = normalize(vec3(0.6, 1.0, 0.4));
    float diffuse = max(dot(normalize(fragNormal), sunDir), 0.0);
    float light   = clamp(0.2 + diffuse * 0.8, 0.0, 1.0);

    vec3 baseCol = texture(atlas, fragUV).rgb;
    outColor = vec4(baseCol * light, 1.0);
}
