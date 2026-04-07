#version 450

layout(location = 0) in vec2  fragUV;
layout(location = 1) in float fragAge;      // 0=fresh 1=expired
layout(location = 2) in float fragComplexity; // 0-1 based on source lines
layout(location = 3) in float fragGlow;     // 0-1 based on mana spent

layout(push_constant) uniform PC {
    mat4  viewProj;
    vec4  color;
    vec4  glowColor;
    vec4  params;
    vec4  mana;
} pc;

layout(location = 0) out vec4 outColor;

// ── Noise ─────────────────────────────────────────────────────────────────────
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
float noise(vec2 p) {
    vec2 i = floor(p); vec2 f = fract(p);
    f = f*f*(3.0-2.0*f);
    return mix(mix(hash(i),           hash(i+vec2(1,0)), f.x),
               mix(hash(i+vec2(0,1)), hash(i+vec2(1,1)), f.x), f.y);
}

// ── SDF helpers ───────────────────────────────────────────────────────────────
float sdCircle(vec2 p, float r) { return length(p) - r; }

float sdLine(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p-a, ba = b-a;
    float h = clamp(dot(pa,ba)/dot(ba,ba), 0.0, 1.0);
    return length(pa - ba*h);
}

float sdRegularPolygon(vec2 p, float r, int n) {
    float angle = 6.28318 / float(n);
    float a = atan(p.y, p.x);
    a = mod(a, angle) - angle * 0.5;
    return cos(a) * length(p) - r;
}

// Draw a ring with thickness t
float ring(vec2 p, float r, float t) {
    return smoothstep(t, 0.0, abs(sdCircle(p, r)));
}

// Draw a regular polygon outline
float polyOutline(vec2 p, float r, int n, float t) {
    return smoothstep(t, 0.0, abs(sdRegularPolygon(p, r, n)));
}

// Star polygon connecting every k-th vertex of n-gon
float starLines(vec2 p, float r, int n, int k, float t) {
    float result = 0.0;
    float step   = 6.28318 / float(n);
    for (int i = 0; i < n; i++) {
        float a0 = step * float(i);
        float a1 = step * float(mod(float(i + k), float(n)));
        vec2  v0 = vec2(cos(a0), sin(a0)) * r;
        vec2  v1 = vec2(cos(a1), sin(a1)) * r;
        result   = max(result, smoothstep(t, 0.0, sdLine(p, v0, v1)));
    }
    return result;
}

// Tick marks around a ring
float tickMarks(vec2 p, float r, int count, float len, float t) {
    float result = 0.0;
    float step   = 6.28318 / float(count);
    for (int i = 0; i < count; i++) {
        float a  = step * float(i);
        vec2  d  = vec2(cos(a), sin(a));
        vec2  v0 = d * r;
        vec2  v1 = d * (r - len);
        result   = max(result, smoothstep(t, 0.0, sdLine(p, v0, v1)));
    }
    return result;
}

// Rune glyphs — simple geometric marks placed at angle a, distance r from center
float runeGlyph(vec2 p, float r, float angle, int glyphType, float t) {
    // Rotate and translate to glyph center
    vec2 gc  = vec2(cos(angle), sin(angle)) * r;
    vec2 lp  = p - gc;
    // Rotate local space to face outward
    float ca = cos(-angle); float sa = sin(-angle);
    vec2  rp = vec2(ca*lp.x - sa*lp.y, sa*lp.x + ca*lp.y);

    float s  = 0.06; // glyph scale
    float result = 0.0;

    if (glyphType == 0) {
        // Cross
        result = max(smoothstep(t,0.0,sdLine(rp,vec2(-s,0.0),vec2(s,0.0))),
                     smoothstep(t,0.0,sdLine(rp,vec2(0.0,-s),vec2(0.0,s))));
    } else if (glyphType == 1) {
        // Triangle
        result = max(smoothstep(t,0.0,sdLine(rp,vec2(-s,-s*0.6),vec2(s,-s*0.6))),
                 max(smoothstep(t,0.0,sdLine(rp,vec2(-s,-s*0.6),vec2(0.0,s*0.8))),
                     smoothstep(t,0.0,sdLine(rp,vec2(s,-s*0.6),vec2(0.0,s*0.8)))));
    } else if (glyphType == 2) {
        // Arrow pointing outward
        result = max(smoothstep(t,0.0,sdLine(rp,vec2(-s,0.0),vec2(s,0.0))),
                 max(smoothstep(t,0.0,sdLine(rp,vec2(0.0,s),vec2(s,0.0))),
                     smoothstep(t,0.0,sdLine(rp,vec2(0.0,-s),vec2(s,0.0)))));
    } else if (glyphType == 3) {
        // Diamond
        result = max(smoothstep(t,0.0,sdLine(rp,vec2(-s,0.0),vec2(0.0,s))),
                 max(smoothstep(t,0.0,sdLine(rp,vec2(0.0,s),vec2(s,0.0))),
                 max(smoothstep(t,0.0,sdLine(rp,vec2(s,0.0),vec2(0.0,-s))),
                     smoothstep(t,0.0,sdLine(rp,vec2(0.0,-s),vec2(-s,0.0))))));
    } else {
        // Circle dot
        result = smoothstep(t,0.0,sdCircle(rp,s*0.5));
    }
    return result;
}

void main() {
    vec2  uv   = fragUV * 2.0 - 1.0;  // [-1,1]
    float dist = length(uv);
    float t    = 0.018; // line thickness
    float age  = fragAge;
    float cx   = fragComplexity;      // 0=simple 1=complex
    float glow = fragGlow;            // 0=low mana 1=high mana

    // Discard outside the circle
    if (dist > 1.05) discard;

    float sig = 0.0;

    // ── Layer 0: always — outer ring + inner ring ─────────────────────────────
    sig = max(sig, ring(uv, 0.92, t));
    sig = max(sig, ring(uv, 0.82, t * 0.6));

    // ── Layer 1: always — center dot ─────────────────────────────────────────
    sig = max(sig, smoothstep(0.05, 0.0, sdCircle(uv, 0.08)));

    // ── Layer 2: complexity > 0.1 — triangle inscribed in outer ring ─────────
    if (cx > 0.1) {
        sig = max(sig, polyOutline(uv, 0.82, 3, t));
    }

    // ── Layer 3: complexity > 0.25 — tick marks on outer ring ────────────────
    if (cx > 0.25) {
        int ticks = 12 + int(cx * 24.0); // 12-36 ticks
        sig = max(sig, tickMarks(uv, 0.92, ticks, 0.06, t));
    }

    // ── Layer 4: complexity > 0.35 — second polygon ───────────────────────────
    if (cx > 0.35) {
        sig = max(sig, polyOutline(uv, 0.60, 4, t));
    }

    // ── Layer 5: complexity > 0.45 — star lines connecting triangle verts ─────
    if (cx > 0.45) {
        sig = max(sig, starLines(uv, 0.82, 6, 2, t));
    }

    // ── Layer 6: complexity > 0.55 — rune glyphs around middle ring ──────────
    if (cx > 0.55) {
        int glyphCount = 3 + int(cx * 5.0); // 3-8 glyphs
        float step = 6.28318 / float(glyphCount);
        for (int i = 0; i < glyphCount; i++) {
            float angle = step * float(i);
            int gtype   = int(mod(float(i) * 1.618, 5.0)); // golden ratio spread
            sig = max(sig, runeGlyph(uv, 0.71, angle, gtype, t));
        }
    }

    // ── Layer 7: complexity > 0.70 — third polygon + star ────────────────────
    if (cx > 0.70) {
        sig = max(sig, polyOutline(uv, 0.45, 5, t));
        sig = max(sig, starLines(uv, 0.60, 4, 1, t * 0.8));
    }

    // ── Layer 8: complexity > 0.85 — inner rune glyphs ───────────────────────
    if (cx > 0.85) {
        int innerCount = 4;
        float step = 6.28318 / float(innerCount);
        for (int i = 0; i < innerCount; i++) {
            float angle = step * float(i) + 0.785; // 45 deg offset
            int gtype   = int(mod(float(i) * 2.414, 5.0));
            sig = max(sig, runeGlyph(uv, 0.35, angle, gtype, t * 0.8));
        }
    }

    // ── Glow intensity from mana ──────────────────────────────────────────────
    // Base pulse — slower for high mana spells (more stable)
    float pulseSpeed = mix(4.0, 1.5, glow);
    float pulse      = 0.7 + 0.3 * sin(pc.params.x * pulseSpeed);

    // High mana: add a shimmer noise layer
    float shimmer = 0.0;
    if (glow > 0.3) {
        shimmer = noise(uv * 4.0 + pc.params.x * 0.8) * 0.15 * glow;
    }

    // Edge ground glow — bleeds outward, stronger with more mana
    float edgeDist  = 1.0 - dist;
    float groundGlow = smoothstep(0.0, 0.3, edgeDist) *
                       smoothstep(1.0, 0.6, edgeDist) *
                       glow * 0.4 * pulse;

    // ── Fade out over lifetime ────────────────────────────────────────────────
    float fadeOut = 1.0 - smoothstep(0.75, 1.0, age);

    // ── Colour ────────────────────────────────────────────────────────────────
    vec3 core = pc.color.rgb;
    vec3 halo = pc.glowColor.rgb;

    // Lines are core colour, background glow is halo colour
    vec3 col  = mix(halo * 0.3, core, sig);
    // Glow intensity boosts brightness
    col      += halo * groundGlow;
    col      += core * shimmer;
    col      *= mix(0.6, 1.4, glow) * pulse; // brighter with more mana

    float alpha = (max(sig, groundGlow) + shimmer) * fadeOut;
    alpha       = clamp(alpha * mix(0.7, 1.0, glow), 0.0, 1.0);

    if (alpha < 0.005) discard;
    outColor = vec4(col, alpha);
}
