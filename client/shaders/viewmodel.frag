#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

void main() {
    vec3  sunDir  = normalize(vec3(0.6, 1.0, 0.4));
    float diffuse = max(dot(normalize(fragNormal), sunDir), 0.0);
    float light   = clamp(0.15 + diffuse * 0.85, 0.0, 1.0);

    // Flat grey until textures are added
    vec3 baseCol = vec3(0.72, 0.70, 0.68);
    outColor = vec4(baseCol * light, 1.0);
}
