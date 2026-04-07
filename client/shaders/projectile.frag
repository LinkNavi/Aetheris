#version 450

layout(push_constant) uniform PC {
    mat4  viewProj;
    vec4  sphereWorldPos;
    vec4  color;
    vec4  glowColor;
    vec4  camPos;
    vec4  params; // x=element, y=sunIntensity, z=outerRadius, w=unused
} pc;

layout(location = 0) in vec3 fragRayOrig;
layout(location = 1) in vec3 fragRayDir;
layout(location = 2) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

// ── Noise helpers ─────────────────────────────────────────────────────────────
float hash(vec3 p) {
    p = fract(p * vec3(443.8975, 397.2973, 491.1871));
    p += dot(p.zxy, p.yxz + 19.19);
    return fract(p.x * p.y * p.z);
}

float noise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f*f*(3.0-2.0*f);
    return mix(
        mix(mix(hash(i),           hash(i+vec3(1,0,0)),f.x),
            mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),
        mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),
            mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),
        f.z);
}

float fbm(vec3 p) {
    return noise(p)*0.5 + noise(p*2.1)*0.25 + noise(p*4.3)*0.125;
}

// ── Ray-sphere intersection ───────────────────────────────────────────────────
// Returns vec2(tNear, tFar), or vec2(-1) on miss
vec2 raySphere(vec3 ro, vec3 rd, vec3 center, float radius) {
    vec3  oc  = ro - center;
    float b   = dot(oc, rd);
    float c   = dot(oc, oc) - radius*radius;
    float dis = b*b - c;
    if (dis < 0.0) return vec2(-1.0);
    float s = sqrt(dis);
    return vec2(-b - s, -b + s);
}

// ── Element effects on outer sphere surface ───────────────────────────────────
// hitPos: world position on outer sphere surface
// normal: outward normal at hit
// age: seconds since spawn (for animation)
vec4 elementEffect(vec3 hitPos, vec3 normal, float age, int element,
                   vec3 coreCol, vec3 glowCol) {
    float t = age;

    if (element == 1) { // Fire
        float n = fbm(hitPos * 2.0 + vec3(0, -t * 2.0, 0));
        float flame = smoothstep(0.3, 0.8, n);
        vec3 col = mix(vec3(1.0,0.1,0.0), vec3(1.0,0.7,0.0), flame);
        return vec4(col, flame * 0.7);

    } else if (element == 2) { // Ice
        float n = noise(hitPos * 4.0 + t * 0.2);
        float crystal = smoothstep(0.4, 0.9, n);
        vec3 col = mix(vec3(0.5,0.8,1.0), vec3(1.0,1.0,1.0), crystal);
        return vec4(col, 0.3 + crystal * 0.4);

    } else if (element == 3) { // Lightning
        // Animated sin-based lightning lines
        float angle = atan(normal.y, normal.x) + t * 3.0;
        float bolt  = abs(sin(angle * 8.0 + noise(hitPos*3.0)*6.28));
        bolt = smoothstep(0.85, 1.0, bolt);
        return vec4(vec3(0.9,0.9,0.2) + bolt, bolt * 0.9);

    } else if (element == 4) { // Void
        float n = fbm(hitPos * 1.5 + t * 0.5);
        float swirl = smoothstep(0.2, 0.7, n);
        vec3 col = mix(vec3(0.1,0.0,0.3), vec3(0.5,0.0,1.0), swirl);
        return vec4(col, 0.5 + swirl * 0.3);

    } else if (element == 5) { // Arcane
        float n = fbm(hitPos * 2.0 + t);
        vec3 col = mix(glowCol, vec3(1.0), n * 0.5);
        return vec4(col, 0.3 + n * 0.4);

    } else if (element == 6) { // Nature
        float n = fbm(hitPos * 3.0 + vec3(t*0.3));
        vec3 col = mix(vec3(0.1,0.5,0.1), vec3(0.4,1.0,0.2), n);
        return vec4(col, 0.4 + n * 0.3);

    } else if (element == 7) { // Wind
        float n = noise(hitPos * 2.0 + vec3(t*1.5, 0, t));
        float ribbon = smoothstep(0.6, 0.9, n);
        return vec4(vec3(0.8,0.9,1.0), ribbon * 0.4);

    } else if (element == 8) { // Earth
        float n = fbm(hitPos * 2.5);
        vec3 col = mix(vec3(0.3,0.2,0.1), vec3(0.6,0.4,0.2), n);
        return vec4(col, 0.5 + n * 0.2);

    } else if (element == 9) { // Water
        float n = fbm(hitPos * 2.0 + vec3(0, t*0.5, t*0.3));
        vec3 col = mix(vec3(0.1,0.3,0.8), vec3(0.4,0.7,1.0), n);
        return vec4(col, 0.3 + n * 0.5);
    }

    // None / default
    float n = noise(hitPos * 2.0 + t * 0.5);
    return vec4(glowCol, 0.3 + n * 0.2);
}

void main() {
    vec3  ro      = fragRayOrig;
    vec3  rd      = normalize(fragRayDir);
    vec3  sPos    = pc.sphereWorldPos.xyz;
    float innerR  = pc.sphereWorldPos.w;
    float outerR  = pc.params.z;
    float age     = pc.glowColor.a;
    int   element = int(pc.params.x);
    float sunI    = pc.params.y;
    vec3  coreCol = pc.color.rgb;
    vec3  glowCol = pc.glowColor.rgb;

    vec3 sunDir = normalize(vec3(0.6, 1.0, 0.4));

    // ── Test outer sphere ─────────────────────────────────────────────────────
    vec2 outerHit = raySphere(ro, rd, sPos, outerR);
    if (outerHit.x < 0.0) discard;

    // ── Test inner sphere ─────────────────────────────────────────────────────
    vec2 innerHit = raySphere(ro, rd, sPos, innerR);
    bool hitInner = (innerHit.x >= 0.0 && innerHit.x < outerHit.y);

    if (hitInner) {
        // ── Solid core ────────────────────────────────────────────────────────
        vec3 hitPos = ro + rd * innerHit.x;
        vec3 normal = normalize(hitPos - sPos);

        float diff  = max(dot(normal, sunDir), 0.0) * sunI;
        float amb   = mix(0.3, 0.6, sunI);
        float light = clamp(amb + diff * 0.7, 0.0, 1.0);

        // Fresnel rim
        float fresnel = pow(1.0 - max(dot(-rd, normal), 0.0), 3.0);
        vec3  rimCol  = mix(coreCol, glowCol, fresnel);
        vec3  col     = rimCol * light + glowCol * fresnel * 0.5;

        // Bright core highlight
        col += coreCol * 0.4;

        outColor = vec4(col, 1.0);

    } else {
        // ── Outer transparent shell ───────────────────────────────────────────
        vec3 hitPos = ro + rd * outerHit.x;
        vec3 normal = normalize(hitPos - sPos);

        vec4 effect = elementEffect(hitPos, normal, age, element, coreCol, glowCol);

        // Fresnel — more opaque at grazing angles
        float fresnel = pow(1.0 - abs(dot(rd, normal)), 2.0);
        float alpha   = clamp(effect.a + fresnel * 0.3, 0.0, 0.95);

        // Additive glow contribution
        vec3 col = effect.rgb + glowCol * fresnel * 0.4;

        outColor = vec4(col, alpha);
    }
}
