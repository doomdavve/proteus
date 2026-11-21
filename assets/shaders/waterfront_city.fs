#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragWorldPosition;
in vec3 fragWorldNormal;
in float fragOcclusion;
flat in vec4 fragObjectColor;
flat in int fragEmissive;
flat in int fragFacade;

uniform sampler2D texture0;
uniform float ghostOpacity;
uniform float ghostPixelScale;
uniform int groundPass;
uniform int facadePass;
uniform int reflectionPass;
uniform vec3 ambientColor;
uniform float ambientOcclusion; // Share of ambient a fully closed baked corner removes.
uniform vec3 directionalColor;
uniform vec3 directionalDirection;
uniform float directionalIntensity;
uniform mat4 lightViewProjection;
uniform sampler2D shadowMap;
uniform int shadowsEnabled;
uniform int farShadowsEnabled;
uniform sampler2D farShadowMap;
uniform mat4 farLightViewProjection;
uniform float farShadowTexel; // Metres per far shadow-map texel.
uniform vec3 cameraPosition;
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;
uniform float fogOpacity;
uniform int atmosphereEnabled;
// Windows pass: vertex alpha is the occupied share. Zero window light keeps daytime glass.
uniform int windowPass;
uniform float windowLight;
uniform vec3 windowLit;
uniform vec3 windowDark;
// Window mask (facade pass): the windows of migrated buildings (scenery/building_windows.h)
// beyond windowMaskRange metres from windowMaskAnchor (X/Z), where they are not quads. facadeData
// holds 3 RGBA32F texels per facade record, 256 records a row: (seed low 16 bits, seed high 16
// bits, bays, floors), (first bay, bay pitch, first floor, floor pitch), (ground width and
// height, upper width and height).
uniform int windowMask;
uniform vec2 windowMaskAnchor;
uniform float windowMaskRange;
uniform highp sampler2D facadeData;
uniform int localLightCount;
uniform vec4 localLightPositionRadius[32];
uniform vec4 localLightColorIntensity[32];
uniform vec4 localLightDirection[32];
uniform vec4 localLightCone[32];
// G5b clusters (waterfront_lights.h): 0 the capped uniform list above, 1 the slot list of each
// point, 2 every clustered light (the reference the slot lists must match exactly).
uniform int lightClusterMode;
uniform highp sampler2D lightClusters; // Float records: keep full precision on the web.
uniform int lightClusterCount;
uniform vec4 lightClusterLayout; // Cell metres, grid slots per axis, slot base, index base.
uniform float emissiveGain; // Above 1 only for an HDR target at dusk.

out vec4 finalColor;

// World-space, metre-scaled ground finishes. No bitmap tiling, painted windows,
// or camera-relative noise. Subpixel detail fades to its mean in both scene passes.
float ground_hash(vec2 cell)
{
    vec3 p = fract(vec3(cell.xyx) * vec3(.1031, .1030, .0973));
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float ground_noise(vec2 p)
{
    vec2 cell = floor(p), t = fract(p);
    t = t * t * (3.0 - 2.0 * t);
    return mix(mix(ground_hash(cell), ground_hash(cell + vec2(1, 0)), t.x),
        mix(ground_hash(cell + vec2(0, 1)), ground_hash(cell + vec2(1, 1)), t.x), t.y) - .5;
}

float ground_detail(vec2 p, float scale, float pixelWidth)
{
    float visible = 1.0 - smoothstep(scale * .25, scale, pixelWidth);
    return ground_noise(p / scale) * visible;
}

float joint_integral(float x, float coverage)
{
    return floor(x) * coverage + min(fract(x), coverage);
}

float ground_joint(float position, float period, float width, float pixelWidth)
{
    float coverage = width / period;
    float x = position / period + coverage * .5;
    float footprint = max(pixelWidth / period, .001);
    // Integrate the periodic seam over the pixel; do not inflate a 2 cm joint into
    // a full dark pixel. Fully unresolved patterns converge to average coverage.
    float line = (joint_integral(x + footprint * .5, coverage)
        - joint_integral(x - footprint * .5, coverage)) / footprint;
    return mix(clamp(line, 0.0, 1.0), coverage, smoothstep(.3, 1.0, footprint));
}

vec3 ground_albedo()
{
    vec2 p = fragWorldPosition.xz;
    vec2 footprint = max(abs(dFdx(p)), abs(dFdy(p)));
    float pixelWidth = max(footprint.x, footprint.y);
    int kind = int(fragTexCoord.x + .5); // WaterfrontGroundMaterial, constant per triangle.
    float weather = ground_detail(p, 7.0, pixelWidth);
    if (kind == 1) { // Asphalt: quiet aggregate and broad wear, no paving grid.
        float aggregate = ground_detail(p, .16, pixelWidth);
        float wear = ground_detail(p + vec2(19, 41), 1.7, pixelWidth);
        return fragColor.rgb * (.91 + .09 * weather + .07 * wear + .10 * aggregate);
    }
    if (kind == 4) { // Planted beds: irregular foliage/soil colour, not concrete slabs.
        float clumps = ground_detail(p, .65, pixelWidth);
        float patches = ground_detail(p + vec2(7, 13), 2.4, pixelWidth);
        return fragColor.rgb * (.97 + .20 * clumps + .14 * patches);
    }
    if (kind == 2) { // 80 x 40 cm staggered paving; two-centimetre joints.
        float row = floor(p.y / .4);
        float x = p.x + mod(row, 2.0) * .4;
        float joint = max(ground_joint(x, .8, .02, footprint.x),
            ground_joint(p.y, .4, .02, footprint.y));
        float tile = ground_hash(vec2(floor(x / .8), row)) - .5;
        tile *= 1.0 - smoothstep(.08, .3, pixelWidth);
        return fragColor.rgb * (.97 + .07 * weather + .08 * tile - .16 * joint);
    }
    // Quay/open concrete: 3 m construction panels, restrained seams and weathering.
    float joint = max(ground_joint(p.x, 3.0, .025, footprint.x),
        ground_joint(p.y, 3.0, .025, footprint.y));
    float grain = ground_detail(p, .3, pixelWidth);
    return fragColor.rgb * (.95 + .12 * weather + .05 * grain - .12 * joint);
}

vec3 facade_albedo()
{
    vec2 p = fragTexCoord; // wall-local metres, same origin for both wall triangles
    vec2 footprint = max(abs(dFdx(p)), abs(dFdy(p)));
    float pixelWidth = max(footprint.x, footprint.y);
    int kind = int(fragColor.a * 255.0 + .5);
    float weather = ground_detail(p, 6.0, pixelWidth);
    vec3 color = fragColor.rgb;
    if (kind == 1) { // Brick: 26 x 9 cm courses including 9 mm mortar.
        float row = floor(p.y / .09);
        float x = p.x + mod(row, 2.0) * .13;
        float mortar = max(ground_joint(x, .26, .009, footprint.x),
            ground_joint(p.y, .09, .009, footprint.y));
        float brick = ground_hash(vec2(floor(x / .26), row)) - .5;
        brick *= 1.0 - smoothstep(.025, .085, pixelWidth);
        color *= .97 + .08 * brick + .07 * weather;
        color = mix(color, vec3(.60, .56, .48), mortar * .55);
    } else if (kind == 2) { // Plaster: quiet render, with a restrained floor stringcourse.
        float grain = ground_detail(p, .12, pixelWidth);
        float belt = ground_joint(p.y - .5, 3.2, .10, footprint.y);
        color *= .98 + .06 * weather + .045 * grain - .10 * belt;
    } else { // Metal/composite panels: 1 m modules and window-aligned 3.2 m floors.
        float joint = max(ground_joint(p.x - .5, 1.0, .018, footprint.x),
            ground_joint(p.y - .5, 3.2, .025, footprint.y));
        float panel = ground_hash(floor((p - .5) / vec2(1.0, 3.2))) - .5;
        panel *= 1.0 - smoothstep(.2, .8, pixelWidth);
        color *= .97 + .06 * panel + .04 * weather - .22 * joint;
    }
    // A masonry base anchors each facade below the existing ground-floor windows.
    // Filter the broad boundary too; this is not an extra painted opening grid.
    float aa = max(footprint.y, .001);
    float base = 1.0 - smoothstep(1.05 - aa * .5, 1.05 + aa * .5, p.y);
    return mix(color, color * vec3(.69, .72, .76), base);
}

uint fmix32(uint h)
{
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

// The share of this pixel a migrated facade's window covers, and its colour: the same grid, seed,
// glass and lit rule as building_windows.c, filtered by the pixel's footprint. Many windows under
// one pixel (a footprint near the pitch) settle on the mean of the palette and the lit share, so
// the far facade neither shimmers nor pops against the quads nearer in.
float window_mask(out vec3 color)
{
    color = vec3(0.0);
    // Derivatives come first: the tests below differ between pixels of one quad.
    vec2 p = fragTexCoord; // wall-local metres: along the wall, above its base
    vec2 footprint = max(max(abs(dFdx(p)), abs(dFdy(p))), vec2(1e-4));
    if (windowMask == 0 || fragFacade <= 0
        || distance(fragWorldPosition.xz, windowMaskAnchor) < windowMaskRange)
        return 0.0;
    int id = fragFacade - 1;
    ivec2 at = ivec2(3 * (id % 256), id / 256);
    vec4 seedBays = texelFetch(facadeData, at, 0);
    vec4 grid = texelFetch(facadeData, at + ivec2(1, 0), 0);
    vec4 sizes = texelFetch(facadeData, at + ivec2(2, 0), 0);
    if (p.x < grid.x || p.y < grid.z || p.x >= grid.x + seedBays.z * grid.y
        || p.y >= grid.z + seedBays.w * grid.w)
        return 0.0;
    float bay = floor((p.x - grid.x) / grid.y);
    float level = floor((p.y - grid.z) / grid.w);
    vec2 size = level < 0.5 ? sizes.xy : sizes.zw;
    vec2 pitch = grid.yw;
    vec2 local = p - grid.xz - vec2(bay, level) * pitch;
    vec2 lo = max(local - footprint * 0.5, vec2(0.0));
    vec2 hi = min(local + footprint * 0.5, size);
    vec2 inside = clamp((hi - lo) / footprint, 0.0, 1.0);
    float mean = size.x * size.y / (pitch.x * pitch.y);
    float settle = smoothstep(0.4, 1.0, max(footprint.x / pitch.x, footprint.y / pitch.y));
    float cover = mix(inside.x * inside.y, mean, settle);
    uint seed = fmix32((uint(seedBays.x) | (uint(seedBays.y) << 16)) + uint(bay) * 0x9E3779B1u
        + uint(level) * 0x85EBCA77u);
    uint kind = seed % 7u;
    vec3 glass = kind < 3u ? vec3(138.0, 185.0, 204.0) / 255.0
        : kind == 3u ? vec3(181.0, 211.0, 221.0) / 255.0 : vec3(66.0, 100.0, 120.0) / 255.0;
    vec3 palette = (3.0 * vec3(138.0, 185.0, 204.0) + vec3(181.0, 211.0, 221.0)
                       + 3.0 * vec3(66.0, 100.0, 120.0)) / (7.0 * 255.0);
    float lit = float(((((seed ^ (seed >> 15)) * 2654435761u) >> 29) < 3u));
    glass = mix(glass, palette, settle);
    lit = mix(lit, 0.375, settle);
    // The reveal bands building_window_faces draws on the pane: a dark band under the lintel, a mid
    // one on the left jamb and a light sill, each with its lit share. Beyond a pixel per window
    // they settle toward the mean (a little darker than the glass), like the cover.
    float sill = 1.0 - step(size.y * 0.08, local.y);
    float lintel = step(size.y * (1.0 - 0.14), local.y);
    float jamb = step(local.x, size.x * 0.08);
    vec3 band = glass;
    float share = lit;
    if (sill > 0.0) {
        band = (glass + vec3(232.0, 228.0, 218.0) / 255.0) * 0.5;
        share = 0.0;
    } else if (lintel > 0.0) {
        band = glass * 0.4;
        share = lit * 77.0 / 255.0;
    } else if (jamb > 0.0) {
        band = glass * 0.7;
        share = lit * 153.0 / 255.0;
    }
    band = mix(band, glass * 0.88, settle);
    share = mix(share, lit * 0.85, settle);
    color = band;
    if (windowLight > 0.0)
        color = mix(band, mix(band * windowDark, windowLit, share), windowLight);
    return cover;
}

vec3 apply_atmosphere(vec3 color)
{
    if (atmosphereEnabled == 0)
        return color;

    float distanceToCamera = length(fragWorldPosition - cameraPosition);
    float fog = smoothstep(fogStart, fogEnd, distanceToCamera) * fogOpacity;
    return mix(color, fogColor, fog);
}

// Square light-space coverage; blend before the detailed map ends, then fade the far map.
float shadow_visibility(float bias, vec3 farOffset)
{
    vec4 nearLight = lightViewProjection * vec4(fragWorldPosition, 1.0);
    vec3 p = nearLight.xyz / nearLight.w * 0.5 + 0.5;
    float edge = max(abs(p.x * 2.0 - 1.0), abs(p.y * 2.0 - 1.0));
    float nearVisibility = 1.0;
    if (all(greaterThan(p, vec3(0.0))) && all(lessThan(p, vec3(1.0))))
        nearVisibility = p.z - bias > texture(shadowMap, p.xy).r ? 0.0 : 1.0;
    if (farShadowsEnabled == 0)
        return nearVisibility;

    float nearWeight = 1.0 - smoothstep(0.70, 0.95, edge);
    if (nearWeight >= 1.0)
        return nearVisibility;
    vec4 farLight = farLightViewProjection * vec4(fragWorldPosition + farOffset, 1.0);
    vec3 q = farLight.xyz / farLight.w * 0.5 + 0.5;
    float farVisibility = 1.0;
    if (all(greaterThan(q, vec3(0.0))) && all(lessThan(q, vec3(1.0)))) {
        // Match the near map's world-space bias across the longer 2200 m depth span.
        farVisibility = q.z - bias * (1400.0 / 2200.0) > texture(farShadowMap, q.xy).r
            ? 0.0 : 1.0;
        float farEdge = max(abs(q.x * 2.0 - 1.0), abs(q.y * 2.0 - 1.0));
        farVisibility = mix(farVisibility, 1.0, smoothstep(0.80, 0.98, farEdge));
    }
    return mix(farVisibility, nearVisibility, nearWeight);
}

float directional_shadow(vec3 normal)
{
    if (shadowsEnabled == 0)
        return 1.0;
    float incidence = max(0.0, dot(normal, normalize(directionalDirection)));
    float bias = max(0.0012 * (1.0 - incidence), 0.00018);
    // A depth bias cannot cover a far texel's 1.5 m: a low sun striped sunlit walls with their
    // own shadow. Look the far map up a texel out along the normal, more as the light grazes.
    vec3 farOffset = normal * (farShadowTexel * sqrt(1.0 - incidence * incidence));
    // Remove direct fill in shadow; ambient and local/emissive light remain visible.
    return shadow_visibility(bias, farOffset);
}

vec3 localLight(vec4 positionRadius, vec4 colorIntensity, vec4 direction, vec4 cone,
    vec3 normal)
{
    vec3 delta = positionRadius.xyz - fragWorldPosition;
    float radius = positionRadius.w;
    float distance = length(delta);
    if (distance >= radius)
        return vec3(0.0);
    vec3 towardLight = delta / max(distance, 0.001);
    float incidence = max(0.0, dot(normal, towardLight));
    float attenuation = 1.0 - distance / radius;
    float spot = 1.0;
    if (cone.y >= 0.0)
        spot = smoothstep(cone.y, cone.x, dot(normalize(direction.xyz), -towardLight));
    return colorIntensity.rgb * (colorIntensity.a * incidence * attenuation * attenuation * spot);
}

vec4 lightTexel(int index)
{
    return texelFetch(lightClusters, ivec2(index % 256, index / 256), 0);
}

vec3 clusterLight(int light, vec3 normal)
{
    int base = light * 4;
    return localLight(lightTexel(base), lightTexel(base + 1), lightTexel(base + 2),
        lightTexel(base + 3), normal);
}

void addLocalLights(inout vec3 illumination, vec3 normal)
{
    if (lightClusterMode == 0) {
        for (int index = 0; index < localLightCount; ++index)
            illumination += localLight(localLightPositionRadius[index],
                localLightColorIntensity[index], localLightDirection[index],
                localLightCone[index], normal);
        return;
    }
    if (lightClusterMode == 2) {
        for (int light = 0; light < lightClusterCount; ++light)
            illumination += clusterLight(light, normal);
        return;
    }
    vec2 cell = mod(floor(fragWorldPosition.xz / lightClusterLayout.x), lightClusterLayout.y);
    int slot = int(cell.y) * int(lightClusterLayout.y) + int(cell.x);
    vec4 header = lightTexel(int(lightClusterLayout.z) + slot);
    int offset = int(header.x), count = int(header.y);
    for (int k = 0; k < count; ++k) {
        int at = offset + k;
        vec4 slots = lightTexel(int(lightClusterLayout.w) + at / 4);
        illumination += clusterLight(int(slots[at % 4]), normal);
    }
}

void main()
{
    if (ghostOpacity > 0.0) {
        const int bayer[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6,
            3, 11, 1, 9, 15, 7, 13, 5);
        ivec2 cell = ivec2(floor(gl_FragCoord.xy / ghostPixelScale)) & 3;
        if ((float(bayer[cell.y * 4 + cell.x]) + .5) / 16.0 >= ghostOpacity)
            discard;
        finalColor = vec4(90.0 / 255.0, 240.0 / 255.0, 1.0, 1.0);
        return;
    }
    vec4 albedo = texture(texture0, fragTexCoord) * fragObjectColor * fragColor;
    if (groundPass != 0)
        albedo = vec4(ground_albedo(), fragColor.a) * fragObjectColor;
    if (facadePass != 0)
        albedo = vec4(facade_albedo(), 1.0) * fragObjectColor;
    if (fragEmissive != 0) {
        if (reflectionPass != 0 && fragWorldPosition.y < 0.0)
            discard;
        if (windowPass != 0) {
            if (windowLight > 0.0)
                albedo.rgb = mix(albedo.rgb,
                    mix(albedo.rgb * windowDark, windowLit, fragColor.a), windowLight);
            albedo.a = 1.0;
        }
        // The 8-bit reflection keeps unit gain; its streaks come from the water.
        float gain = reflectionPass != 0 ? 1.0 : emissiveGain;
        finalColor = vec4(apply_atmosphere(albedo.rgb * gain), albedo.a);
        return;
    }

    vec3 normal = normalize(fragWorldNormal);
    float directional = max(0.0, dot(normal, normalize(directionalDirection)));
    // A facade is one tall triangle per wall, so its base corners' occlusion would shade the
    // whole height: keep it to the bottom metres, as contact with the street.
    float occlusion = fragOcclusion;
    if (facadePass != 0)
        occlusion *= 1.0 - smoothstep(0.0, 4.0, fragTexCoord.y);
    vec3 illumination = ambientColor * (1.0 - ambientOcclusion * occlusion)
        + directionalColor * directionalIntensity * directional * directional_shadow(normal);
    addLocalLights(illumination, normal);

    // Preserve the full output palette while retaining the low-resolution framebuffer. Clamp
    // illumination before multiplying albedo, rather than adding white, so nearby lamps do not
    // bleach materials.
    vec3 lit = clamp(illumination, 0.0, 1.25);
    vec3 wall = apply_atmosphere(min(albedo.rgb * lit, vec3(1.0)));
    if (facadePass != 0) {
        // Windows beyond the quads' range, unlit and fogged like the window pass draws them.
        vec3 glass;
        float cover = window_mask(glass);
        if (cover > 0.0)
            wall = mix(wall, apply_atmosphere(glass * (reflectionPass != 0 ? 1.0 : emissiveGain)),
                cover);
    }
    finalColor = vec4(wall, albedo.a);
    // The mirrored camera sits underwater. Submerged quay walls and hull bottoms
    // must not occlude the above-water scene or appear as a second shoreline.
    // Clip after the material derivatives, which need complete fragment quads.
    if (reflectionPass != 0 && fragWorldPosition.y < 0.0)
        discard;
}
