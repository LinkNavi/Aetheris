#version 450

layout(location = 0) in vec3  fragNormal;
layout(location = 1) in float sunIntensity;

layout(push_constant) uniform PC {
    mat4 viewProj;
    vec4 params;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec3  n        = normalize(fragNormal);
    vec3  sunDir   = normalize(vec3(0.6, 1.0, 0.4));
    float diffuse  = max(dot(n, sunDir), 0.0) * sunIntensity;
    float ambient  = mix(0.05, 0.2, sunIntensity);
    float light    = clamp(ambient + diffuse, 0.0, 1.0);

    // Simple terrain colour by normal angle (grass top, dirt side)
    vec3 grassCol = vec3(0.33, 0.56, 0.18);
    vec3 dirtCol  = vec3(0.50, 0.35, 0.22);
    float slope   = clamp(n.y, 0.0, 1.0);
    vec3  baseCol = mix(dirtCol, grassCol, smoothstep(0.6, 0.8, slope));

    outColor = vec4(baseCol * light, 1.0);
}
