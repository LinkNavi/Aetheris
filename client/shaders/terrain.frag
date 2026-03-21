#version 450

layout(set = 1, binding = 0) uniform sampler2D atlas;

layout(location = 0) in vec3  fragNormal;
layout(location = 1) in float sunIntensity;
layout(location = 2) in vec2  fragUV;
layout(location = 3) in float fragDist;
layout(location = 4) in flat uint fragTint;

layout(push_constant) uniform PC {
    mat4 viewProj;
    vec4 params;  // x=sunIntensity, y=fogStart, z=fogEnd
    vec4 camPos;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 an = abs(fragNormal);
    vec3 n;
    if      (an.x > an.y && an.x > an.z) n = vec3(sign(fragNormal.x), 0, 0);
    else if (an.y > an.x && an.y > an.z) n = vec3(0, sign(fragNormal.y), 0);
    else                                  n = vec3(0, 0, sign(fragNormal.z));

    vec3  sunDir  = normalize(vec3(0.6, 1.0, 0.4));
    float diffuse = max(dot(n, sunDir), 0.0) * sunIntensity;
    float ambient = mix(0.03, 0.12, sunIntensity); // darker ambient
    float light   = clamp(ambient + diffuse * 0.65, 0.0, 1.0); // weaker diffuse

    vec3 baseCol = texture(atlas, fragUV).rgb;

    // Unpack biome tint
    float tr = float((fragTint >>  8) & 0xFF) / 128.0;
    float tg = float((fragTint >> 16) & 0xFF) / 128.0;
    float tb = float((fragTint >> 24) & 0xFF) / 128.0;
    vec3 tint = vec3(tr, tg, tb);

    vec3 lit = baseCol * tint * light;

    // Desaturate the whole terrain slightly for a bleaker look
    float lum = dot(lit, vec3(0.299, 0.587, 0.114));
    lit = mix(lit, vec3(lum), 0.25);

    // Darken overall
    lit *= 0.72;

    // Fog — use a darker, more overcast sky color
    float fogStart  = pc.params.y;
    float fogEnd    = pc.params.z;
    float fogFactor = clamp((fragDist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    float sun       = sunIntensity;
    vec3  fogColor  = mix(vec3(0.01, 0.01, 0.04), vec3(0.28, 0.32, 0.38), sun);

    outColor = vec4(mix(lit, fogColor, fogFactor), 1.0);
}
