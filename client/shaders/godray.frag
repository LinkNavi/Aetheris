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

// ── Low-end path: single cheap ray march, 8 samples, no multi-ray ────────────
// Full quality path is preserved behind QUALITY_GODRAYS define (never set
// for this target) so the file documents intent without dead code removal.

float skyOcclusion(vec2 uv) {
    float depth = texture(sceneDepth, uv).r;
    if (depth < 0.99995) return 0.0;
    vec3  col      = texture(sceneColor, uv).rgb;
    float lum      = dot(col, vec3(0.2126, 0.7152, 0.0722));
    float blueness = col.b / max(lum, 0.001);
    float clearSky  = smoothstep(0.92, 1.20, blueness);
    float thinCloud = smoothstep(0.5, 0.8, lum) * (1.0 - clearSky) * 0.25;
    return clearSky + thinCloud;
}

void main() {
    vec3 scene = texture(sceneColor, fragUV).rgb;

    // Early-out: skip the entire pass when sun is weak or off screen.
    // On Intel HD 3000 this eliminates the pass entirely at night / dawn.
    float sunEdgeDist = max(abs(pc.sunScreenPos.x - 0.5), abs(pc.sunScreenPos.y - 0.5));
    float sunVis = 1.0 - smoothstep(0.35, 0.65, sunEdgeDist);
    if (pc.intensity < 0.15 || sunVis < 0.05) {
        outColor = vec4(scene, 1.0);
        return;
    }

    // ── Single-ray, 8-sample march (was 8 rays × 32 samples = 256 taps) ─────
    // 8 taps vs 256 = 32× cheaper. Still looks acceptable at low resolution.
    const int NUM_SAMPLES = 8;

    vec2  dir     = fragUV - pc.sunScreenPos;
    float dirLen  = length(dir);
    if (dirLen < 0.001) { outColor = vec4(scene, 1.0); return; }

    float stepSize = (dirLen * pc.density) / float(NUM_SAMPLES);
    vec2  step     = -(dir / dirLen) * stepSize;

    float decay  = 1.0;
    float illum  = 0.0;

    for (int i = 0; i < NUM_SAMPLES; i++) {
        vec2 sampleUV = clamp(fragUV + step * float(i), 0.001, 0.999);
        illum += skyOcclusion(sampleUV) * decay * 0.12;
        decay *= pc.decay;
    }

    // Distance fade keeps it subtle at edges
    float distFade = 1.0 - smoothstep(0.3, 0.9, dirLen);

    vec3  rayCol     = mix(vec3(0.92, 0.78, 0.55), vec3(0.88, 0.90, 0.92), pc.intensity);
    float rayStrength = min(illum * pc.intensity, 0.04);
    outColor = vec4(scene + rayCol * rayStrength * sunVis * distFade, 1.0);
}
