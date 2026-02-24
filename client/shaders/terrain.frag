#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in float fragSunIntensity;

layout(location = 0) out vec4 outColor;

void main() {
    vec3  lightDir = normalize(vec3(0.6, 1.0, 0.4));
    float diff     = max(dot(normalize(fragNormal), lightDir), 0.0);

    float ambient = mix(0.05, 0.2, fragSunIntensity);
    float diffuse = diff * fragSunIntensity;
    float light   = ambient + diffuse * 0.8;

    vec3 color = vec3(0.3, 0.7, 0.3) * light;
    outColor   = vec4(color, 1.0);
}
