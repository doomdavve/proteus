#version 330

in vec2 historyUv;

// 0: one fixed substep of foam history and ripples, read from previousState and written to the
// other target. 1: write fillValue (clears, and the water coverage of the mask).
uniform int historyMode;
uniform vec4 fillValue;
uniform sampler2D previousState; // R foam, G ripple height now, B ripple height one step ago.
uniform sampler2D waterMask; // R: 1 over water drawn from the map's water meshes.
uniform sampler2D stampField; // The G3a field: A is this frame's crest foam.
uniform vec4 stampRect; // Origin X/Z, 1/extent, half-texel guard in UV; z = 0 means no field.
uniform vec3 historyOrigin; // World X/Z of texel (0, 0)'s corner, metres per texel.
uniform ivec2 historyShift; // Whole texels the origin moved since the previous substep.
uniform int historyStep; // Global substep index, for deterministic rain.
uniform vec4 foamCoefficients; // Decay per substep, seed gain, drift X/Z in texels per substep.
uniform vec4 rippleCoefficients; // Courant squared, damping per substep, height limit, unused.
uniform int sourceCount;
uniform vec4 sources[16]; // World X/Z, radius, height impulse.
uniform int hullCount;
uniform vec4 hulls[8]; // World X/Z, radius: no ripples or foam inside.
uniform vec4 rain; // World X/Z, radius, drop chance per texel per substep.
uniform float rainImpulse;

out vec4 finalColor;

ivec2 historySize;

vec4 previous(ivec2 texel)
{
    texel += historyShift;
    if (any(lessThan(texel, ivec2(0))) || any(greaterThanEqual(texel, historySize)))
        return vec4(0.0);
    return texelFetch(previousState, texel, 0);
}

// Bilinear foam from the previous state at a fractional texel, zero beyond the edge.
float previousFoam(vec2 texel)
{
    vec2 base = floor(texel - 0.5);
    vec2 f = texel - 0.5 - base;
    ivec2 b = ivec2(base);
    float a = previous(b).r, c = previous(b + ivec2(1, 0)).r;
    float d = previous(b + ivec2(0, 1)).r, e = previous(b + ivec2(1, 1)).r;
    return mix(mix(a, c, f.x), mix(d, e, f.x), f.y);
}

uint hash(ivec3 p)
{
    uint h = uint(p.x) * 1664525u ^ uint(p.y) * 22695477u ^ uint(p.z) * 2654435761u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

void main()
{
    if (historyMode == 1) {
        finalColor = fillValue;
        return;
    }
    historySize = textureSize(previousState, 0);
    ivec2 texel = ivec2(gl_FragCoord.xy);
    vec2 world = historyOrigin.xy + (vec2(texel) + 0.5) * historyOrigin.z;
    bool open = texelFetch(waterMask, texel, 0).r > 0.5;
    for (int i = 0; i < 8; ++i) {
        if (i >= hullCount) break;
        open = open && distance(world, hulls[i].xy) >= hulls[i].z;
    }
    if (!open) {
        finalColor = vec4(0.0);
        return;
    }

    // Foam: drift and decay what was there, then keep the larger of it and this frame's crests.
    float foam = previousFoam(vec2(texel) + 0.5 - foamCoefficients.zw) * foamCoefficients.x;
    if (stampRect.z > 0.0) {
        vec2 uv = (world - stampRect.xy) * stampRect.z;
        if (all(greaterThanEqual(uv, vec2(stampRect.w)))
            && all(lessThanEqual(uv, vec2(1.0 - stampRect.w))))
        {
            // The crest foam water shading shows: foam scaled by how high the crest stands.
            vec4 s = textureLod(stampField, uv, 0.0);
            float crest = clamp(s.a, 0.0, 1.0) * clamp(s.r / 0.3, 0.0, 1.0);
            foam = max(foam, clamp(crest * foamCoefficients.y, 0.0, 1.0));
        }
    }

    // Ripples: a damped explicit wave equation; closed texels hold zero height, so quays and
    // hulls reflect. The Courant number stays far below the 2D limit of 1/sqrt(2).
    vec4 here = previous(texel);
    float laplacian = previous(texel + ivec2(1, 0)).g + previous(texel - ivec2(1, 0)).g
        + previous(texel + ivec2(0, 1)).g + previous(texel - ivec2(0, 1)).g - 4.0 * here.g;
    float height
        = (2.0 * here.g - here.b + rippleCoefficients.x * laplacian) * rippleCoefficients.y;
    for (int i = 0; i < 16; ++i) {
        if (i >= sourceCount) break;
        float d = distance(world, sources[i].xy) / sources[i].z;
        if (d < 1.0) height += sources[i].w * (1.0 - d * d);
    }
    if (rain.w > 0.0 && distance(world, rain.xy) < rain.z) {
        ivec2 cell = ivec2(floor(world / historyOrigin.z));
        if (float(hash(ivec3(cell, historyStep)) & 0xffffffu) < rain.w * 16777216.0)
            height += rainImpulse;
    }
    height = clamp(height, -rippleCoefficients.z, rippleCoefficients.z);
    finalColor = vec4(foam, height, here.g, 0.0);
}
