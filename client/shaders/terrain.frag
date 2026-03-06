#version 450

layout(set = 1, binding = 0) uniform sampler2D atlas;

layout(location = 0) in vec3  fragNormal;
layout(location = 1) in float sunIntensity;
layout(location = 2) in vec2  fragUV;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 an = abs(fragNormal);
    vec3 n;
    if (an.x > an.y && an.x > an.z)      n = vec3(sign(fragNormal.x), 0, 0);
    else if (an.y > an.x && an.y > an.z) n = vec3(0, sign(fragNormal.y), 0);
    else                                  n = vec3(0, 0, sign(fragNormal.z));

    vec3  sunDir  = normalize(vec3(0.6, 1.0, 0.4));
    float diffuse = max(dot(n, sunDir), 0.0) * sunIntensity;
    float ambient = mix(0.05, 0.2, sunIntensity);
    float light   = clamp(ambient + diffuse, 0.0, 1.0);

    vec3 baseCol = texture(atlas, fragUV).rgb;
    outColor = vec4(baseCol * light, 1.0);
}
