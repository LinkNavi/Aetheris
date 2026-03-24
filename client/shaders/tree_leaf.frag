#version 450

layout(location = 0) in vec2  fragUV;
layout(location = 1) in float fragNoise;
layout(location = 2) in float fragAlpha;
layout(location = 3) in float fragDist;

layout(push_constant) uniform PC {
    mat4  viewProj;
    vec4  params;  // x=windTime, y=sunIntensity, z=fogStart, w=fogEnd
    vec4  camPos;
    vec4  sunDir;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    if (fragAlpha < 0.05) discard;

    float sunIntensity = pc.params.y;

    vec3 leafDark  = vec3(0.04, 0.10, 0.03);
    vec3 leafMid   = vec3(0.07, 0.16, 0.05);
    vec3 leafLight = vec3(0.10, 0.22, 0.06);
    float n = clamp(fragNoise, 0.0, 1.0);
    vec3 leafCol = n > 0.6 ? mix(leafMid, leafLight, (n-0.6)/0.4)
                           : mix(leafDark, leafMid, n/0.6);
    float lum = dot(leafCol, vec3(0.299, 0.587, 0.114));
    leafCol = mix(leafCol, vec3(lum*0.6), 0.35);

    float ambient = mix(0.15, 0.35, sunIntensity);
    vec3 lit = leafCol * (ambient + 0.3 * fragAlpha);

    float fogFactor = clamp((fragDist - pc.params.z) / (pc.params.w - pc.params.z), 0.0, 1.0);
    float dusk      = clamp(1.0 - abs(pc.sunDir.y - 0.10) / 0.28, 0.0, 1.0);
    dusk *= dusk;
    vec3 fogColor = mix(mix(vec3(0.01,0.01,0.04), vec3(0.52,0.62,0.75), sunIntensity),
                        vec3(0.60,0.28,0.10), dusk * 0.5);

    float alpha = fragAlpha * 0.88 * clamp(1.0 - fogFactor * 0.85, 0.0, 1.0);
    outColor = vec4(mix(lit, fogColor, fogFactor * 0.7), alpha);
}
