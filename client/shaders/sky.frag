#version 450

layout(location = 0) in vec3 fragRayDir;

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
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f*f*(3.0-2.0*f);
    return mix(mix(hash(i),           hash(i+vec2(1,0)), f.x),
               mix(hash(i+vec2(0,1)), hash(i+vec2(1,1)), f.x), f.y);
}
float fbmCloud(vec2 p) {
    float v=0.0, amp=0.5, freq=1.0;
    for (int i=0; i<6; i++) {
        v    += vnoise(p*freq)*amp;
        amp  *= 0.5;
        freq *= 2.1;
    }
    return v;
}

float miePhase(float cosTheta) {
    const float g=0.76, g2=g*g;
    return (1.0-g2)/(4.0*3.14159*pow(1.0+g2-2.0*g*cosTheta,1.5));
}

void main() {
    vec3  ray    = normalize(fragRayDir);
    vec3  sun    = normalize(pc.sunDir.xyz);
    float sunI   = clamp(pc.params.x, 0.0, 1.0);
    float time   = pc.params.y;
    float cspeed = pc.params.z;
    vec3  camPos = pc.camPos.xyz;

    // Flip Y if needed — Vulkan NDC has Y down, so ray.y may be inverted
    // We want ray.y > 0 = looking up
    // The invViewProj already accounts for the Vulkan Y flip in the proj matrix
    // so fragRayDir should be correct. If sky appears upside down, negate ray.y.

    // Below horizon
    if (ray.y < -0.05) {
        outColor = vec4(0.01, 0.01, 0.015, 1.0);
        return;
    }

    float cosTheta = dot(ray, sun);
    float cosSunUp = sun.y; // how high sun is (1=zenith, 0=horizon, -1=midnight)

    // Height in sky [0=horizon, 1=zenith]
    float h       = clamp(ray.y, 0.0, 1.0);
    float hSmooth = h*h*(3.0-2.0*h);

    // Dusk/dawn factor — sun near horizon
    float dusk = clamp(1.0 - abs(cosSunUp - 0.10) / 0.28, 0.0, 1.0);
    dusk *= dusk;

    // ── Sky gradient ──────────────────────────────────────────────────────────
    vec3 zenithNight  = vec3(0.004, 0.004, 0.020);
    vec3 zenithDay    = vec3(0.10,  0.25,  0.65);
    vec3 horizNight   = vec3(0.007, 0.007, 0.025);
    vec3 horizDay     = vec3(0.45,  0.62,  0.85);
    vec3 horizDusk    = vec3(0.72,  0.28,  0.05);

    vec3 horizCol = mix(horizNight, horizDay, sunI);
    horizCol      = mix(horizCol,   horizDusk, dusk * 0.7);
    vec3 zenithCol= mix(zenithNight, zenithDay, sunI);
    vec3 skyCol   = mix(horizCol, zenithCol, hSmooth);

    // Rayleigh — blue scatter near horizon
    skyCol += vec3(0.0, 0.015, 0.05) * sunI * (1.0-hSmooth) * 0.6;

    // Mie — glow around sun
    float mie  = miePhase(cosTheta) * sunI;
    vec3 mieC  = mix(vec3(1.0, 0.50, 0.12), vec3(1.0, 0.90, 0.78), sunI);
    skyCol    += mieC * mie * 0.10;

    // ── Sun disk ──────────────────────────────────────────────────────────────
    float sunDisk  = smoothstep(0.9996, 0.99985, cosTheta);
    vec3  sunColor = mix(vec3(1.0, 0.35, 0.07), vec3(1.1, 1.02, 0.82), sunI);
    skyCol        += sunColor * sunDisk * (0.3 + sunI * 0.7);

    // ── Stars ─────────────────────────────────────────────────────────────────
    float night = clamp(1.0 - sunI*2.5, 0.0, 1.0);
    if (night > 0.01 && ray.y > 0.0) {
        vec2 starUV   = vec2(atan(ray.x, ray.z), asin(clamp(ray.y,0.0,1.0))) * 16.0;
        vec2 starCell = floor(starUV);
        float sh      = hash(starCell);
        float bright  = step(0.968, sh);
        vec2  off     = vec2(hash(starCell+0.1), hash(starCell+0.2)) - 0.5;
        float dist    = length(fract(starUV) - 0.5 + off*0.25);
        float star    = bright * smoothstep(0.16, 0.0, dist) * (0.3 + fract(sh*137.5)*0.7);
        skyCol       += vec3(0.70, 0.78, 1.0) * star * night * night;
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

    // ── Clouds ────────────────────────────────────────────────────────────────
    if (ray.y > 0.01) {
        float cloudAlt = 800.0;
        float t        = (cloudAlt - camPos.y) / max(ray.y, 0.01);

        if (t > 0.0 && t < 15000.0) {
            vec2 worldXZ = camPos.xz + ray.xz * t;
            vec2 uv      = worldXZ * 0.000075 + vec2(time*cspeed*0.007, time*cspeed*0.004);

            float cloud  = fbmCloud(uv);
            float thresh = 0.44;
            float cover  = smoothstep(thresh, thresh+0.22, cloud);

            // Fade near horizon and at distance
            cover *= smoothstep(0.01, 0.12, ray.y);
            cover *= clamp(1.0 - t/12000.0, 0.0, 1.0);

            vec3 cLight  = mix(vec3(0.09,0.09,0.12), vec3(0.68,0.74,0.80), sunI);
            vec3 cShadow = mix(vec3(0.03,0.03,0.05), vec3(0.25,0.28,0.33), sunI);
            float edge   = clamp(cosTheta*0.5+0.5, 0.0, 1.0);
            vec3  cCol   = mix(cShadow, cLight, edge);
            cCol         = mix(cCol, vec3(0.78,0.35,0.10), dusk*cover*0.4);

            skyCol = mix(skyCol, cCol, cover * 0.88);
        }
    }

    outColor = vec4(skyCol, 1.0);
}
