#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragWorldPosition;
in vec3 fragWorldNormal;
flat in vec4 fragObjectColor;
flat in int fragEmissive;

uniform sampler2D texture0;
uniform float ghostOpacity;
uniform float ghostPixelScale;
uniform int groundPass;
uniform int facadePass;
uniform int reflectionPass;
uniform vec3 ambientColor;
uniform vec3 directionalColor;
uniform vec3 directionalDirection;
uniform float directionalIntensity;
uniform mat4 lightViewProjection;
uniform sampler2D shadowMap;
uniform int shadowsEnabled;
uniform int farShadowsEnabled;
uniform sampler2D farShadowMap;
uniform mat4 farLightViewProjection;
uniform vec3 cameraPosition;
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;
uniform float fogOpacity;
uniform int atmosphereEnabled;
uniform int localLightCount;
uniform vec4 localLightPositionRadius[32];
uniform vec4 localLightColorIntensity[32];
uniform vec4 localLightDirection[32];
uniform vec4 localLightCone[32];

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

vec3 apply_atmosphere(vec3 color)
{
    if (atmosphereEnabled == 0)
        return color;

    float distanceToCamera = length(fragWorldPosition - cameraPosition);
    float fog = smoothstep(fogStart, fogEnd, distanceToCamera) * fogOpacity;
    return mix(color, fogColor, fog);
}

// Square light-space coverage; blend before the detailed map ends, then fade the far map.
float shadow_visibility(float bias)
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
    vec4 farLight = farLightViewProjection * vec4(fragWorldPosition, 1.0);
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
    // Remove direct fill in shadow; ambient and local/emissive light remain visible.
    return shadow_visibility(bias);
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
        finalColor = vec4(apply_atmosphere(albedo.rgb), albedo.a);
        return;
    }

    vec3 normal = normalize(fragWorldNormal);
    float directional = max(0.0, dot(normal, normalize(directionalDirection)));
    vec3 illumination = ambientColor
        + directionalColor * directionalIntensity * directional * directional_shadow(normal);
    for (int index = 0; index < localLightCount; ++index) {
        vec3 delta = localLightPositionRadius[index].xyz - fragWorldPosition;
        float radius = localLightPositionRadius[index].w;
        float distance = length(delta);
        if (distance >= radius)
            continue;
        vec3 towardLight = delta / max(distance, 0.001);
        float incidence = max(0.0, dot(normal, towardLight));
        float attenuation = 1.0 - distance / radius;
        float spot = 1.0;
        float outerCone = localLightCone[index].y;
        if (outerCone >= 0.0) {
            float cone = dot(normalize(localLightDirection[index].xyz), -towardLight);
            float innerCone = localLightCone[index].x;
            spot = smoothstep(outerCone, innerCone, cone);
        }
        illumination += localLightColorIntensity[index].rgb
            * (localLightColorIntensity[index].a * incidence * attenuation * attenuation * spot);
    }

    // Preserve the full output palette while retaining the low-resolution framebuffer. Clamp
    // illumination before multiplying albedo, rather than adding white, so nearby lamps do not
    // bleach materials.
    vec3 lit = clamp(illumination, 0.0, 1.25);
    finalColor = vec4(apply_atmosphere(min(albedo.rgb * lit, vec3(1.0))), albedo.a);
    // The mirrored camera sits underwater. Submerged quay walls and hull bottoms
    // must not occlude the above-water scene or appear as a second shoreline.
    // Clip after the material derivatives, which need complete fragment quads.
    if (reflectionPass != 0 && fragWorldPosition.y < 0.0)
        discard;
}
