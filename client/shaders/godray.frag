#version 450

layout(set = 0, binding = 0) uniform sampler2D sceneColor;  // rendered scene
layout(set = 0, binding = 1) uniform sampler2D sceneDepth;  // depth buffer

layout(push_constant) uniform PC {
    vec2  sunScreenPos;   // sun position in UV space [0,1]
    float intensity;      // overall ray strength (0 at night, 1 at noon)
    float exposure;       // tone-map scale
    float decay;          // how fast rays fade with distance from sun
    float density;        // sample step scale
    float weight;         // per-sample brightness
    float pad;
} pc;

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 scene = texture(sceneColor, fragUV).rgb;

    // Only run rays when sun is somewhat above horizon
    if (pc.intensity < 0.02) {
        outColor = vec4(scene, 1.0);
        return;
    }

    // Radial blur from sun toward current pixel
    const int   NUM_SAMPLES = 80;
    vec2  delta     = (fragUV - pc.sunScreenPos) * pc.density / float(NUM_SAMPLES);
    vec2  uv        = fragUV;
    float illum     = 0.0;
    float decay     = 1.0;

    for (int i = 0; i < NUM_SAMPLES; i++) {
        uv -= delta;
        // Clamp to screen
        vec2 suv = clamp(uv, 0.001, 0.999);

        // A pixel contributes to god rays only if it's sky (depth == 1.0)
        // Terrain pixels occlude — they block the ray
        float depth = texture(sceneDepth, suv).r;
        float sky   = step(0.9999, depth); // 1 if sky, 0 if terrain

        // Also use the scene brightness at that point as occlusion mask
        float bright = dot(texture(sceneColor, suv).rgb, vec3(0.333));
        illum += sky * bright * decay * pc.weight;
        decay *= pc.decay;
    }

    // Color of god rays: warm golden near noon, orange at dusk/dawn
    vec3 rayColor = mix(vec3(0.9, 0.5, 0.15), vec3(1.0, 0.92, 0.70), pc.intensity);

    // Additive blend — rays add on top of scene
    vec3 result = scene + rayColor * illum * pc.intensity * pc.exposure;

    // Simple reinhard tone map to keep it from blowing out
    result = result / (result + 1.0);

    outColor = vec4(result, 1.0);
}
