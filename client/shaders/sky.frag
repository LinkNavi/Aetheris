#version 450
// Low-end sky shader for Intel HD 3000 / Sandy Bridge.
// Changes vs full quality:
//   - cloudFbm:       6 octaves → 3 octaves
//   - cloudFbmDetail: 3 octaves → 2 octaves
//   - Aurora:         3 layers  → 1 layer (or skipped at daylight)
//   - Stars:          2 passes  → 1 pass
//   - Cloud layer 1:  warp pass removed
//   - Cloud layer 2:  kept but uses cheaper fbm

layout(location = 0) in vec2 fragNDC;

layout(push_constant) uniform PC {
    mat4  invViewProj;
    vec4  sunDir;
    vec4  params;  // x=sunIntensity, y=time, z=cloudSpeed, w=0
    vec4  camPos;
} pc;

layout(location = 0) out vec4 outColor;

// ── Noise (unchanged — hash is cheap) ────────────────────────────────────────
float hash(vec2 p) {
    p = fract(p * vec2(234.34, 435.345));
    p += dot(p, p + 34.23);
    return fract(p.x * p.y);
}
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f*f*(3.0-2.0*f);
    return mix(mix(hash(i),           hash(i+vec2(1,0)), f.x),
               mix(hash(i+vec2(0,1)), hash(i+vec2(1,1)), f.x), f.y);
}

// ── Cloud FBM: 3 octaves (was 6) ─────────────────────────────────────────────
float cloudFbm(vec2 p, float time) {
    float v   = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    v += vnoise(p * freq + vec2(time * 0.012, time * 0.008)) * amp;
    amp *= 0.55; freq *= 2.1;
    v += vnoise(p * freq + vec2(time * 0.018, -time * 0.010)) * amp;
    amp *= 0.55; freq *= 2.1;
    v += vnoise(p * freq - vec2(time * 0.022, time * 0.014)) * amp;
    return v;
}

// ── Cirrus detail: 2 octaves (was 3) ─────────────────────────────────────────
float cloudFbmDetail(vec2 p, float time) {
    float v   = 0.0;
    float amp = 0.5;
    float freq = 1.8;
    v += vnoise(p * freq + vec2(time * 0.020, time * 0.013)) * amp;
    amp *= 0.5; freq *= 2.1;
    v += vnoise(p * freq - vec2(time * 0.028, time * 0.018)) * amp;
    return v;
}

float miePhase(float cosTheta) {
    const float g=0.76, g2=g*g;
    return (1.0-g2)/(4.0*3.14159*pow(1.0+g2-2.0*g*cosTheta,1.5));
}

// ── Aurora: 1 layer only, skipped above sunIntensity 0.1 ─────────────────────
float auroraWave(vec2 p, float t) {
    float wave = 0.0;
    wave += sin(p.x * 1.2 + t * 0.3) * 0.5;
    wave += sin(p.x * 2.7 - t * 0.5 + 1.3) * 0.3;
    wave += vnoise(p * 1.5 + vec2(t * 0.1, 0.0)) * 0.6;
    return wave;
}

vec3 aurora(vec3 ray, float night, float time) {
    if (night < 0.15 || ray.y < 0.05) return vec3(0.0);
    float t = 1.0 / max(ray.y, 0.05);
    vec2 uv = ray.xz * t * 0.15;
    // Single layer only
    float wave = auroraWave(uv, time);
    float band = ray.y * 3.0;
    float vertFade = smoothstep(0.3, 0.8, band) * smoothstep(2.5, 1.2, band);
    float intensity = smoothstep(0.45, 0.8, wave) * vertFade;
    intensity *= smoothstep(0.0, 0.18, ray.y);
    vec3 col = mix(vec3(0.10, 0.42, 0.65), vec3(0.05, 0.52, 0.48),
                   vnoise(uv * 0.7 + vec2(time * 0.05)));
    return col * intensity * night * night * 0.30;
}

void main() {
    vec4 world = pc.invViewProj * vec4(fragNDC, 1.0, 1.0);
    vec3 ray = normalize(world.xyz / world.w);

    vec3  sun    = normalize(pc.sunDir.xyz);
    float sunI   = clamp(pc.params.x, 0.0, 1.0);
    float time   = pc.params.y;
    float cspeed = pc.params.z;
    vec3  camPos = pc.camPos.xyz;

    if (ray.y < -0.05) {
        outColor = vec4(0.01, 0.01, 0.015, 1.0);
        return;
    }

    float cosTheta = dot(ray, sun);
    float cosSunUp = sun.y;
    float h        = clamp(ray.y, 0.0, 1.0);
    float hSmooth  = h*h*(3.0-2.0*h);
    float dusk     = clamp(1.0 - abs(cosSunUp - 0.10) / 0.28, 0.0, 1.0);
    dusk *= dusk;

    // ── Sky gradient (unchanged — cheap) ─────────────────────────────────────
    vec3 zenithNight = vec3(0.004, 0.006, 0.025);
    vec3 zenithDay   = vec3(0.10,  0.25,  0.65);
    vec3 horizNight  = vec3(0.007, 0.010, 0.030);
    vec3 horizDay    = vec3(0.45,  0.62,  0.85);
    vec3 horizDusk   = vec3(0.72,  0.28,  0.05);

    vec3 horizCol  = mix(horizNight, horizDay, sunI);
    horizCol       = mix(horizCol,   horizDusk, dusk * 0.7);
    vec3 zenithCol = mix(zenithNight, zenithDay, sunI);
    vec3 skyCol    = mix(horizCol, zenithCol, hSmooth);
    skyCol += vec3(0.0, 0.015, 0.05) * sunI * (1.0-hSmooth) * 0.6;

    float mie  = miePhase(cosTheta) * sunI;
    vec3 mieC  = mix(vec3(1.0, 0.50, 0.12), vec3(1.0, 0.90, 0.78), sunI);
    skyCol    += mieC * mie * 0.10;

    // ── Sun disk ──────────────────────────────────────────────────────────────
    float sunDisk  = smoothstep(0.9996, 0.99985, cosTheta);
    vec3  sunColor = mix(vec3(1.0, 0.35, 0.07), vec3(1.1, 1.02, 0.82), sunI);
    skyCol        += sunColor * sunDisk * (0.3 + sunI * 0.7);

    // ── Stars: single pass only ───────────────────────────────────────────────
    float night = clamp(1.0 - sunI * 3.5, 0.0, 1.0);
    if (night > 0.01 && ray.y > 0.0) {
        vec2 starUV = vec2(atan(ray.x, ray.z), asin(clamp(ray.y,0.0,1.0))) * 28.0;
        vec2 cell   = floor(starUV);
        float sh    = hash(cell);
        float bright= step(0.92, sh);
        vec2  off   = vec2(hash(cell+0.1), hash(cell+0.2)) - 0.5;
        float dist  = length(fract(starUV) - 0.5 + off*0.3);
        float star  = bright * smoothstep(0.12, 0.0, dist) * (0.2 + fract(sh*137.5)*0.5);
        float twinkle = 0.6 + 0.4 * sin(time * (2.0 + sh * 4.0) + sh * 50.0);
        skyCol += vec3(0.65, 0.75, 1.0) * star * night * night * twinkle;
    }

    // ── Moon ──────────────────────────────────────────────────────────────────
    if (sunI < 0.3) {
        vec3  moonDir  = -sun;
        float md       = dot(ray, moonDir);
        float moonDisk = smoothstep(0.9993, 0.9997, md);
        float moonGlow = pow(max(md,0.0), 30.0) * 0.010;
        float moonVis  = clamp(1.0 - sunI/0.3, 0.0, 1.0);
        skyCol += vec3(0.68, 0.75, 0.88) * (moonDisk*0.5 + moonGlow) * moonVis;
    }

    // ── Aurora: 1 layer (skip when sun is up) ─────────────────────────────────
    if (sunI < 0.1)
        skyCol += aurora(ray, night, time);

    // ── Clouds: no domain warp (saves 2 vnoise lookups per fragment) ──────────
    if (ray.y > 0.005) {
        float cloudAlt1 = 900.0;
        if (camPos.y < cloudAlt1) {
            float t1 = (cloudAlt1 - camPos.y) / ray.y;
            if (t1 > 0.0 && t1 < 20000.0) {
                vec2 worldXZ  = camPos.xz + ray.xz * t1;
                vec2 uv1      = worldXZ * 0.000055;
                // No warp pass — saves 2 vnoise calls per pixel
                float cloud   = cloudFbm(uv1, time * cspeed);
                float cover   = smoothstep(0.32, 0.50, cloud);
                cover *= smoothstep(0.005, 0.10, ray.y);
                cover *= clamp(1.0 - t1/16000.0, 0.0, 1.0);
                if (cover > 0.001) {
                    float topLight   = smoothstep(0.3, 0.8, cloud);
                    vec3  cLit       = mix(vec3(0.12,0.14,0.20), vec3(0.95,0.97,1.00), sunI);
                    vec3  cShad      = mix(vec3(0.05,0.06,0.10), vec3(0.55,0.58,0.65), sunI);
                    cLit  = mix(cLit,  vec3(0.90,0.45,0.15), dusk*0.5);
                    cShad = mix(cShad, vec3(0.54,0.27,0.09), dusk*0.4);
                    vec3  cloudCol   = mix(cShad, cLit, topLight);
                    if (night > 0.1) cloudCol += vec3(0.03,0.05,0.12)*night*cover;
                    skyCol = mix(skyCol, cloudCol, cover*0.92);
                }
            }
        }
        // Cirrus layer: 2-octave fbm
        float cloudAlt2 = 2200.0;
        if (camPos.y < cloudAlt2) {
            float t2 = (cloudAlt2 - camPos.y) / ray.y;
            if (t2 > 0.0 && t2 < 30000.0) {
                vec2  worldXZ2 = camPos.xz + ray.xz * t2;
                vec2  uv2      = worldXZ2 * 0.000030;
                float cirrus   = cloudFbmDetail(uv2, time*cspeed*0.7);
                float cover2   = smoothstep(0.48, 0.73, cirrus)*0.55;
                cover2 *= smoothstep(0.01, 0.15, ray.y);
                cover2 *= clamp(1.0 - t2/25000.0, 0.0, 1.0);
                if (cover2 > 0.001) {
                    vec3 cirrusCol = mix(vec3(0.70,0.75,0.85), vec3(0.92,0.95,1.00), sunI);
                    cirrusCol = mix(cirrusCol, vec3(0.85,0.55,0.25), dusk*0.6);
                    skyCol = mix(skyCol, cirrusCol, cover2*0.75);
                }
            }
        }
    }

    outColor = vec4(skyCol, 1.0);
}
