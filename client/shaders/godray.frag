#version 450

layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(set = 0, binding = 1) uniform sampler2D sceneDepth;

layout(push_constant) uniform PC {
    vec2  sunScreenPos;
    float intensity;
    float exposure;
    float decay;
    float density;
    float weight;
    float pad;
} pc;

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Returns 1.0 for open sky, 0.0 for terrain/trees, partial for clouds
float skyOcclusion(vec2 uv) {
    float depth = texture(sceneDepth, uv).r;
    float isSky = step(0.99995, depth);
    if (isSky < 0.5) return 0.0;

    // Distinguish clear sky from clouds using blueness ratio
    vec3  col      = texture(sceneColor, uv).rgb;
    float lum      = dot(col, vec3(0.2126, 0.7152, 0.0722));
    float blueness = col.b / max(lum, 0.001);

    // Clear blue sky lets rays through, clouds block them
    float clearSky  = smoothstep(0.92, 1.20, blueness);
    // Thin cloud edges pass a little light
    float thinCloud = smoothstep(0.5, 0.8, lum) * (1.0 - clearSky) * 0.25;

    return clearSky + thinCloud;
}

void main() {
    vec3 scene = texture(sceneColor, fragUV).rgb;

    if (pc.intensity < 0.04) {
        outColor = vec4(scene, 1.0);
        return;
    }

    // Fade out when sun is near or off screen edge
    float sunEdgeDist = max(abs(pc.sunScreenPos.x - 0.5), abs(pc.sunScreenPos.y - 0.5));
    float sunVis = 1.0 - smoothstep(0.4, 0.75, sunEdgeDist);
    if (sunVis < 0.001) {
        outColor = vec4(scene, 1.0);
        return;
    }

    // Also fade based on distance from current pixel to sun — far pixels get weaker rays
    float pixelDist = length(fragUV - pc.sunScreenPos);
    float distFade  = 1.0 - smoothstep(0.3, 0.9, pixelDist);

    const int NUM_RAYS    = 8;
    const int NUM_SAMPLES = 32;

    vec3 totalRay = vec3(0.0);

    for (int r = 0; r < NUM_RAYS; r++) {
        float seed     = float(r) * 0.17343;
        float angleOff = (hash(vec2(seed, 0.1)) - 0.5) * 0.06;
        float lenOff   =  hash(vec2(seed, 0.9)) * 0.25 + 0.75;

        vec2  dir    = fragUV - pc.sunScreenPos;
        float dirLen = length(dir);
        if (dirLen < 0.001) continue;

        float rayWidth = 0.00025 + dirLen * 0.003;

        vec2 dirN = dir / dirLen;
        float c = cos(angleOff), s = sin(angleOff);
        vec2 rotDir = vec2(dirN.x*c - dirN.y*s, dirN.x*s + dirN.y*c);

        float stepSize = (dirLen * lenOff) / float(NUM_SAMPLES);
        float decay    = 1.0;
        float illum    = 0.0;

        for (int i = 0; i < NUM_SAMPLES; i++) {
            float t        = float(i) * stepSize;
            vec2  sampleUV = clamp(fragUV - rotDir * t, 0.001, 0.999);

            vec2  toSample  = sampleUV - pc.sunScreenPos;
            float lateral   = abs(dot(toSample, vec2(-rotDir.y, rotDir.x)));
            float shaftMask = smoothstep(rayWidth, 0.0, lateral);

            float occlude = skyOcclusion(sampleUV);

            illum += shaftMask * occlude * decay * 0.055;
            decay *= 0.94;
        }

        totalRay += vec3(illum);
    }

    // Warm at low sun, neutral at noon
    vec3  rayCol     = mix(vec3(0.92, 0.78, 0.55), vec3(0.88, 0.90, 0.92), pc.intensity);
    float rayStrength = min(dot(totalRay, vec3(0.333)) * pc.intensity, 0.05);
    outColor = vec4(scene + rayCol * rayStrength * sunVis * distFade, 1.0);
}
