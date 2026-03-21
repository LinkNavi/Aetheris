#version 450

layout(location = 0) in vec3  fragPos;
layout(location = 1) in vec3  fragNormal;
layout(location = 2) in vec2  fragUV;
layout(location = 3) in float fragNoise;

layout(push_constant) uniform PC {
    mat4  viewProj;
    vec4  params; // x = windTime
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 sunDir  = normalize(vec3(0.6, 1.0, 0.4));
    float diff   = max(dot(normalize(fragNormal), sunDir), 0.0);
    float light  = clamp(0.08 + diff * 0.55, 0.0, 1.0); // dimmer ambient + weaker diffuse

    vec3 barkDark  = vec3(0.06, 0.04, 0.02);
    vec3 barkLight = vec3(0.11, 0.07, 0.03);

    float grain = fract(sin(fragUV.y * 47.3 + fragUV.x * 13.7) * 4375.5) * 0.06;
    float n = clamp(fragNoise + grain, 0.0, 1.0);

    vec3 barkCol = mix(barkDark, barkLight, n);

    // Desaturate slightly then push toward near-black
    float lum = dot(barkCol, vec3(0.299, 0.587, 0.114));
    barkCol = mix(barkCol, vec3(lum), 0.3);   // partial desaturate
    barkCol *= 0.6;                            // overall darkening

    outColor = vec4(barkCol * light, 1.0);
}
