#version 450

layout(set = 1, binding = 0) uniform sampler2D atlas;

layout(location = 0) in vec3  fragNormal;
layout(location = 1) in float sunIntensity;
layout(location = 2) in vec2  fragUV;
layout(location = 3) in float fragDist;
layout(location = 4) in flat uint fragTint;
layout(location = 5) in vec3  fragSunDir;

layout(push_constant) uniform PC {
    mat4 viewProj;
    vec4 params;  // x=sunIntensity, y=fogStart, z=fogEnd
    vec4 camPos;
    vec4 sunDir;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(fragSunDir);

    // Diffuse — sun only contributes when above horizon (sunIntensity > 0)
    float diffuse = max(dot(N, L), 0.0) * sunIntensity;

    // Ambient — dark at night, brighter at noon, with a subtle sky bounce
    // from above so undersides of overhangs aren't pure black
    float skyBounce = max(dot(N, vec3(0.0, 1.0, 0.0)), 0.0) * 0.08 * sunIntensity;
    float ambient   = mix(0.02, 0.10, sunIntensity) + skyBounce;

    // Half-lambert wrap: softens the terminator between lit and unlit faces
    float wrap    = dot(N, L) * 0.5 + 0.5; // [0,1]
    float wrapDiff = wrap * wrap * sunIntensity * 0.5;

    float light = clamp(ambient + diffuse * 0.6 + wrapDiff * 0.25, 0.0, 1.0);

    vec3 baseCol = texture(atlas, fragUV).rgb;

    // Unpack biome tint
    float tr = float((fragTint >>  8) & 0xFF) / 128.0;
    float tg = float((fragTint >> 16) & 0xFF) / 128.0;
    float tb = float((fragTint >> 24) & 0xFF) / 128.0;
    vec3 tint = vec3(tr, tg, tb);

    vec3 lit = baseCol * tint * light;

    // Slight desaturation
    float lum = dot(lit, vec3(0.299, 0.587, 0.114));
    lit = mix(lit, vec3(lum), 0.25);
    lit *= 0.72;

    // Fog color matches sky: dark blue at night, grey-blue at day
    float fogStart  = pc.params.y;
    float fogEnd    = pc.params.z;
    float fogFactor = clamp((fragDist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);

    // Fog colour: interpolate between night (near black) and day (blue-grey),
    // with a warm tint near the sun direction at dawn/dusk
    vec3 fogDay   = vec3(0.52, 0.62, 0.75);
    vec3 fogNight = vec3(0.01, 0.01, 0.04);
    float dusk = clamp(1.0 - abs(L.y - 0.10) / 0.28, 0.0, 1.0);
    dusk *= dusk;
    vec3 fogDusk  = vec3(0.60, 0.28, 0.10);
    vec3 fogColor = mix(fogNight, fogDay, sunIntensity);
    fogColor      = mix(fogColor, fogDusk, dusk * 0.5);

    outColor = vec4(mix(lit, fogColor, fogFactor), 1.0);
}
