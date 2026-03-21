#version 450

layout(location = 0) in vec2 fragNDC;

layout(push_constant) uniform PC {
    mat4  invViewProj;
    vec4  sunDir;
    vec4  params;  // x=sunIntensity, y=time, z=cloudSpeed, w=0
    vec4  camPos;
} pc;

layout(location = 0) out vec4 outColor;

// ── Noise ─────────────────────────────────────────────────────────────────────
float hash(vec2 p) {
    p = fract(p * vec2(234.34, 435.345));
    p += dot(p, p + 34.23);
    return fract(p.x * p.y);
}
float hash3(vec3 p) {
    p = fract(p * vec3(234.34, 435.345, 321.123));
    p += dot(p, p + 34.23);
    return fract(p.x * p.y + p.y * p.z);
}
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f*f*(3.0-2.0*f);
    return mix(mix(hash(i),           hash(i+vec2(1,0)), f.x),
               mix(hash(i+vec2(0,1)), hash(i+vec2(1,1)), f.x), f.y);
}
float vnoise3(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f*f*(3.0-2.0*f);
    float v000 = hash3(i);
    float v100 = hash3(i+vec3(1,0,0));
    float v010 = hash3(i+vec3(0,1,0));
    float v110 = hash3(i+vec3(1,1,0));
    float v001 = hash3(i+vec3(0,0,1));
    float v101 = hash3(i+vec3(1,0,1));
    float v011 = hash3(i+vec3(0,1,1));
    float v111 = hash3(i+vec3(1,1,1));
    return mix(mix(mix(v000,v100,f.x),mix(v010,v110,f.x),f.y),
               mix(mix(v001,v101,f.x),mix(v011,v111,f.x),f.y),f.z);
}

// Multi-octave cloud fbm with 3D noise for volume feel
float cloudFbm(vec2 p, float time) {
    float v   = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    // Base large shapes
    v += vnoise(p * freq + vec2(time * 0.012, time * 0.008)) * amp;
    amp *= 0.55; freq *= 2.1;
    v += vnoise(p * freq + vec2(time * 0.018, -time * 0.010)) * amp;
    amp *= 0.55; freq *= 2.1;
    v += vnoise(p * freq - vec2(time * 0.022, time * 0.014)) * amp;
    amp *= 0.50; freq *= 2.2;
    v += vnoise(p * freq + vec2(time * 0.030, time * 0.020)) * amp;
    amp *= 0.45; freq *= 2.3;
    // Fine detail
    v += vnoise(p * freq - vec2(time * 0.040, time * 0.025)) * amp;
    amp *= 0.40; freq *= 2.4;
    v += vnoise(p * freq + vec2(time * 0.055, time * 0.035)) * amp;
    return v;
}

// Secondary smaller cloud layer
float cloudFbmDetail(vec2 p, float time) {
    float v   = 0.0;
    float amp = 0.5;
    float freq = 1.8;
    v += vnoise(p * freq + vec2(time * 0.020, time * 0.013)) * amp;
    amp *= 0.5; freq *= 2.1;
    v += vnoise(p * freq - vec2(time * 0.028, time * 0.018)) * amp;
    amp *= 0.5; freq *= 2.2;
    v += vnoise(p * freq + vec2(time * 0.038, -time * 0.022)) * amp;
    return v;
}

float miePhase(float cosTheta) {
    const float g=0.76, g2=g*g;
    return (1.0-g2)/(4.0*3.14159*pow(1.0+g2-2.0*g*cosTheta,1.5));
}

// ── Aurora ────────────────────────────────────────────────────────────────────
float auroraWave(vec2 p, float t) {
    float wave = 0.0;
    wave += sin(p.x * 1.2 + t * 0.3) * 0.5;
    wave += sin(p.x * 2.7 - t * 0.5 + 1.3) * 0.3;
    wave += sin(p.x * 0.8 + t * 0.2 + 2.7) * 0.4;
    wave += vnoise(p * 1.5 + vec2(t * 0.1, 0.0)) * 0.6;
    return wave;
}

vec3 aurora(vec3 ray, float night, float time) {
    if (night < 0.15 || ray.y < 0.05) return vec3(0.0);
    float t = 1.0 / max(ray.y, 0.05);
    vec2 uv = ray.xz * t * 0.15;
    vec3 col = vec3(0.0);
    for (int layer = 0; layer < 3; layer++) {
        float lf = float(layer);
        vec2 luv = uv * (0.8 + lf * 0.3) + vec2(lf * 3.7, lf * 1.3);
        float wave = auroraWave(luv, time + lf * 2.0);
        float band = ray.y * (3.0 + lf * 0.5);
        float vertFade = smoothstep(0.3, 0.8, band) * smoothstep(2.5, 1.2, band);
        float intensity = smoothstep(0.45, 0.8, wave) * vertFade;
        intensity *= smoothstep(0.0, 0.18, ray.y);
        vec3 c1 = vec3(0.10, 0.42, 0.65);
        vec3 c2 = vec3(0.05, 0.52, 0.48);
        vec3 c3 = vec3(0.16, 0.52, 0.72);
        float colorMix = vnoise(luv * 0.7 + vec2(time * 0.05));
        vec3 auroraCol = mix(c1, c2, colorMix);
        auroraCol = mix(auroraCol, c3, smoothstep(0.5, 0.9, wave) * 0.5);
        float shimmer = 0.85 + 0.15 * sin(time * 3.0 + luv.x * 5.0 + lf * 1.7);
        col += auroraCol * intensity * shimmer * (0.12 - lf * 0.03);
    }
    return col * night * night * 0.35;
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

    // ── Sky gradient ──────────────────────────────────────────────────────────
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

    // ── Stars ─────────────────────────────────────────────────────────────────
    float night = clamp(1.0 - sunI * 3.5, 0.0, 1.0); // stricter day cutoff
    if (night > 0.01 && ray.y > 0.0) {
        vec2 starUV1 = vec2(atan(ray.x, ray.z), asin(clamp(ray.y,0.0,1.0))) * 28.0;
        vec2 cell1   = floor(starUV1);
        float sh1    = hash(cell1);
        float bright1= step(0.92, sh1);
        vec2  off1   = vec2(hash(cell1+0.1), hash(cell1+0.2)) - 0.5;
        float dist1  = length(fract(starUV1) - 0.5 + off1*0.3);
        float star1  = bright1 * smoothstep(0.12, 0.0, dist1) * (0.2 + fract(sh1*137.5)*0.5);
        float twinkle1 = 0.6 + 0.4 * sin(time * (2.0 + sh1 * 4.0) + sh1 * 50.0);
        skyCol += vec3(0.65, 0.75, 1.0) * star1 * night * night * twinkle1;

        vec2 starUV2 = vec2(atan(ray.x, ray.z), asin(clamp(ray.y,0.0,1.0))) * 16.0;
        vec2 cell2   = floor(starUV2);
        float sh2    = hash(cell2 + 77.7);
        float bright2= step(0.94, sh2);
        vec2  off2   = vec2(hash(cell2+3.1), hash(cell2+5.2)) - 0.5;
        float dist2  = length(fract(starUV2) - 0.5 + off2*0.25);
        float star2  = bright2 * smoothstep(0.14, 0.0, dist2) * (0.3 + fract(sh2*97.3)*0.7);
        float twinkle2 = 0.7 + 0.3 * sin(time * (1.5 + sh2 * 3.0) + sh2 * 30.0);
        vec3 starCol2 = mix(vec3(0.80, 0.85, 1.0), vec3(1.0, 0.90, 0.75), step(0.5, fract(sh2*43.1)));
        skyCol += starCol2 * star2 * night * night * twinkle2;
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

    // ── Aurora ────────────────────────────────────────────────────────────────
    skyCol += aurora(ray, night, time);

    // ── Clouds ────────────────────────────────────────────────────────────────
    if (ray.y > 0.005) {
        // Layer 1: cumulus at 900m absolute
        float cloudAlt1 = 900.0;
        if (camPos.y < cloudAlt1) {
            float t1 = (cloudAlt1 - camPos.y) / ray.y;
            if (t1 > 0.0 && t1 < 20000.0) {
                vec2 worldXZ  = camPos.xz + ray.xz * t1;
                vec2 uv1      = worldXZ * 0.000055;
                vec2 warp     = vec2(vnoise(uv1*2.3+vec2(time*0.008,0.0)),
                                     vnoise(uv1*2.3+vec2(0.0,time*0.008))) * 0.15;
                float cloud   = mix(cloudFbm(uv1,time*cspeed),
                                    cloudFbm(uv1+warp,time*cspeed), 0.6);
                float cover   = smoothstep(0.32, 0.50, cloud);
                cover *= smoothstep(0.005, 0.10, ray.y);
                cover *= clamp(1.0 - t1/16000.0, 0.0, 1.0);
                if (cover > 0.001) {
                    float shadow     = cloudFbm(uv1*1.3+vec2(0.05), time*cspeed);
                    float topLight   = smoothstep(0.3, 0.8, cloud);
                    float bottomShad = 1.0 - smoothstep(0.3, 0.7, shadow)*0.5;
                    vec3  cLit       = mix(vec3(0.12,0.14,0.20), vec3(0.95,0.97,1.00), sunI);
                    vec3  cShad      = mix(vec3(0.05,0.06,0.10), vec3(0.55,0.58,0.65), sunI);
                    cLit  = mix(cLit,  vec3(0.90,0.45,0.15), dusk*0.5);
                    cShad = mix(cShad, vec3(0.54,0.27,0.09), dusk*0.4);
                    float silver     = pow(max(cosTheta,0.0),4.0)*sunI*0.3;
                    vec3  cloudCol   = mix(cShad, cLit, topLight*bottomShad)+vec3(silver);
                    if (night > 0.1) cloudCol += vec3(0.03,0.05,0.12)*night*cover;
                    skyCol = mix(skyCol, cloudCol, cover*0.92);
                }
            }
        }
        // Layer 2: cirrus at 2200m absolute
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
