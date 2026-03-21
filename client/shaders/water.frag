#version 450

layout(location = 0) in vec2  fragUV;
layout(location = 1) in float fragDepth;
layout(location = 2) in float fragFlow;
layout(location = 3) in float fragDist;
layout(location = 4) in vec3  fragWorldPos;

layout(push_constant) uniform PC {
    mat4  viewProj;
    vec4  params;   // x=waterTime, y=sunIntensity, z=camY, w=0
    vec4  camPos;
} pc;

layout(location = 0) out vec4 outColor;

// Simple value noise for surface detail
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
float noise(vec2 p) {
    vec2 i = floor(p); vec2 f = fract(p);
    f = f*f*(3.0-2.0*f);
    return mix(mix(hash(i), hash(i+vec2(1,0)), f.x),
               mix(hash(i+vec2(0,1)), hash(i+vec2(1,1)), f.x), f.y);
}

void main() {
    float t    = pc.params.x;
    float sun  = pc.params.y;
    float camY = pc.params.z;

    // Scrolling UV for flow animation
    float flowX = cos(fragFlow);
    float flowZ = sin(fragFlow);
    vec2 scrollUV = fragUV + vec2(flowX, flowZ) * t * 0.05;

    // Surface normal from noise — creates ripple detail
    float n1 = noise(scrollUV * 4.0 + vec2(t * 0.3, 0.0));
    float n2 = noise(scrollUV * 6.0 + vec2(0.0, t * 0.4));
    float n3 = noise(scrollUV * 2.0 - vec2(t * 0.2));
    float surfaceNoise = (n1 * 0.5 + n2 * 0.3 + n3 * 0.2);

    // Water color — dark deep blue, lighter shallow
    vec3 deepColor    = vec3(0.02, 0.06, 0.18);
    vec3 shallowColor = vec3(0.05, 0.18, 0.35);
    vec3 foamColor    = vec3(0.55, 0.70, 0.80);

    float depthBlend = clamp(fragDepth * 2.0, 0.0, 1.0);
    vec3 baseColor   = mix(shallowColor, deepColor, depthBlend);

    // Surface specular — sun glint
    vec3 sunDir = normalize(vec3(0.6, 1.0, 0.4));
    vec3 viewDir = normalize(pc.camPos.xyz - fragWorldPos);
    vec3 surfNorm = normalize(vec3(
        (n1 - 0.5) * 0.3,
        1.0,
        (n2 - 0.5) * 0.3
    ));
    float spec = pow(max(dot(reflect(-sunDir, surfNorm), viewDir), 0.0), 32.0);
    spec *= sun * 0.6;

    // Foam on shallow areas and surface noise peaks
    float foamFactor = clamp((1.0 - fragDepth * 3.0) + surfaceNoise * 0.3 - 0.6, 0.0, 1.0);
    vec3 waterColor  = mix(baseColor, foamColor, foamFactor);

    // Lighting
    float ambient = mix(0.1, 0.3, sun);
    float diffuse = max(dot(surfNorm, sunDir), 0.0) * sun * 0.7;
    float light   = clamp(ambient + diffuse, 0.0, 1.0);

    waterColor = waterColor * light + vec3(spec);

    // Fresnel — more transparent when looking straight down, more opaque at grazing
    float fresnel = pow(1.0 - max(dot(surfNorm, viewDir), 0.0), 3.0);
    float alpha   = mix(0.55, 0.92, fresnel + fragDepth * 0.4);

    // Fade with distance to avoid hard edge at fog boundary
    float fogFade = clamp(1.0 - fragDist / 200.0, 0.0, 1.0);
    alpha *= fogFade;

    outColor = vec4(waterColor, alpha);
}
