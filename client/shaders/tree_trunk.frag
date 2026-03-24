#version 450

layout(location = 0) in vec3  fragPos;
layout(location = 1) in vec3  fragNormal;
layout(location = 2) in vec2  fragUV;
layout(location = 3) in float fragNoise;

layout(push_constant) uniform PC {
    mat4  viewProj;
    vec4  params;  // x=windTime, y=sunIntensity, z=fogStart, w=fogEnd
    vec4  camPos;
    vec4  sunDir;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    float sunIntensity = pc.params.y;
    vec3  sunDir       = normalize(pc.sunDir.xyz);

    float diff      = max(dot(normalize(fragNormal), sunDir), 0.0) * sunIntensity;
    float skyBounce = max(dot(normalize(fragNormal), vec3(0,1,0)), 0.0) * 0.06 * sunIntensity;
    float ambient   = mix(0.02, 0.08, sunIntensity) + skyBounce;
    float light     = clamp(ambient + diff * 0.55, 0.0, 1.0);

    float grain  = fract(sin(fragUV.y * 47.3 + fragUV.x * 13.7) * 4375.5) * 0.06;
    float n      = clamp(fragNoise + grain, 0.0, 1.0);
    vec3 barkCol = mix(vec3(0.06, 0.04, 0.02), vec3(0.11, 0.07, 0.03), n);
    float lum    = dot(barkCol, vec3(0.299, 0.587, 0.114));
    barkCol      = mix(barkCol, vec3(lum), 0.3) * 0.6;
    vec3 lit     = barkCol * light;

    float fragDist  = length(fragPos - pc.camPos.xyz);
    float fogFactor = clamp((fragDist - pc.params.z) / (pc.params.w - pc.params.z), 0.0, 1.0);
    float dusk      = clamp(1.0 - abs(sunDir.y - 0.10) / 0.28, 0.0, 1.0);
    dusk *= dusk;
    vec3 fogColor = mix(mix(vec3(0.01,0.01,0.04), vec3(0.52,0.62,0.75), sunIntensity),
                        vec3(0.60,0.28,0.10), dusk * 0.5);

    outColor = vec4(mix(lit, fogColor, fogFactor), 1.0);
}
