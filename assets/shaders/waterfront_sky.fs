#version 330

in vec2 fragTexCoord;
// Static cloud flags at the original 1024x512 panorama texel centres: R underside, G lit rim.
uniform sampler2D skyMask;
uniform vec3 skyForward;
uniform vec3 skyRight;
uniform vec3 skyUp;
uniform vec2 skyScale;
uniform vec3 skyEye;
// The same recipe as waterfront_sky_image (src/waterfront_scene.c), in 0..255 channel values.
uniform vec3 skyHorizon;
uniform vec3 skyZenith;
uniform float skyGradient;
uniform vec3 sunDirection;
uniform vec3 sunHalo;
uniform vec3 sunDisc;
uniform vec3 horizonGlow;
uniform float haloStrength;
uniform float haloRadius;
uniform float discStrength;
uniform float glowStrength;
uniform vec3 cloudUnderside;
uniform vec3 cloudRim;
// A3.5 weather patch: centre X/Z, radius and grown fraction; base and top heights. Keep in
// step with waterfront_squall_sky (src/waterfront_scene.c).
uniform vec4 squallArea;
uniform vec2 squallHeight;
out vec4 finalColor;

const float SKY_PI = 3.14159265358979323846;
const float SKY_WIDTH = 1024.0;
const float SKY_HEIGHT = 512.0;

vec3 skyMix(vec3 a, vec3 b, float t)
{
    t = clamp(t, 0.0, 1.0);
    return mix(a, b, t * t * (3.0 - 2.0 * t));
}

// C's remainderf: the quotient is rounded to the nearest integer, not floored.
float skyRemainder(float x, float y)
{
    return x - y * round(x / y);
}

vec3 squallSky(vec3 sky, vec3 ray)
{
    float grown = squallArea.w, radius = squallArea.z;
    if (grown <= 0.0 || radius <= 0.0) return sky;
    float horizontal = max(length(ray.xz), 1e-4);
    vec2 direction = ray.xz / horizontal;
    float slope = ray.y / horizontal;
    vec2 to = squallArea.xy - skyEye.xz;
    float along = dot(to, direction);
    float miss = dot(to, to) - along * along;
    if (miss >= radius * radius) return sky;
    float chord = sqrt(radius * radius - miss);
    float near = max(along - chord, 0.0), far = along + chord;
    if (far <= 0.0) return sky;
    float rain = 0.0;
    float base = slope > 0.0 ? (squallHeight.x - skyEye.y) / slope : 1e30;
    float inner = radius * radius * 0.85 * 0.85;
    if (miss < inner) {
        float innerChord = sqrt(inner - miss);
        float from = max(along - innerChord, 0.0), to = min(along + innerChord, base);
        if (to > from) {
            vec2 p = skyEye.xz + direction * ((from + to) * 0.5) - squallArea.xy;
            float shafts = 0.5 + 0.5 * sin(5.0 * atan(p.y, p.x) + 4.0 * length(p) / radius);
            float showers = clamp((grown - 0.3) / 0.4, 0.0, 1.0);
            rain = 1.6 * (to - from) / radius * showers * (0.35 + 0.65 * shafts);
        }
    }
    float ceiling = squallHeight.x + (squallHeight.y - squallHeight.x) * grown;
    float from = max(near, base > 1e29 ? far : base);
    float stride = (far - from) / 16.0;
    for (int i = 0; i < 16 && stride > 0.0; ++i) {
        float t = from + stride * (float(i) + 0.5);
        vec2 p = skyEye.xz + direction * t - squallArea.xy;
        float h = (skyEye.y + slope * t - squallHeight.x) / max(ceiling - squallHeight.x, 1.0);
        if (h > 1.0) break;
        float reach = length(p) / radius;
        float angle = atan(p.y, p.x);
        float edge = 1.0 - 0.5 * h * h + 0.07 * sin(5.0 * angle + 7.0 * h)
            + 0.05 * sin(13.0 * angle + 3.0);
        if (reach > edge) continue;
        float lit = clamp(h, 0.0, 1.0);
        float cover = clamp((edge - reach) * 8.0, 0.0, 1.0) * clamp(grown * 3.0, 0.0, 1.0);
        vec3 cloud = (vec3(64.0, 72.0, 84.0) + lit * vec3(92.0, 90.0, 86.0)) / 255.0;
        sky = mix(sky, cloud, cover);
        break;
    }
    return mix(sky, vec3(98.0, 108.0, 120.0) / 255.0, min(rain, 0.85));
}

void main()
{
    vec2 plane = (fragTexCoord * 2.0 - 1.0) * skyScale;
    vec3 ray = normalize(skyForward + plane.x * skyRight - plane.y * skyUp);
    // Sample the original panorama texel exactly as the CPU walk does.
    float u = atan(ray.x, ray.z) / 6.28318530718 + 0.5;
    int sx = int(floor(u * SKY_WIDTH)) % int(SKY_WIDTH);
    int sy = int(clamp((1.0 - ray.y) * 0.5 * SKY_HEIGHT, 0.0, SKY_HEIGHT - 1.0));
    float elevation = 1.0 - 2.0 * ((float(sy) + 0.5) / SKY_HEIGHT);
    float azimuth = ((float(sx) + 0.5) / SKY_WIDTH - 0.5) * 2.0 * SKY_PI;

    vec3 color = skyMix(skyHorizon, skyZenith, elevation / skyGradient);
    float bearing = skyRemainder(azimuth - atan(sunDirection.x, sunDirection.z), 2.0 * SKY_PI);
    if (glowStrength > 0.0) {
        // Low sunlight along the horizon, strongest under the sun and fading to the opposite sky.
        float along = clamp(1.0 - abs(bearing) / 1.7, 0.0, 1.0);
        float low = clamp(1.0 - abs(elevation - max(sunDirection.y, 0.0)) / 0.28, 0.0, 1.0);
        color = skyMix(color, horizonGlow, glowStrength * along * along * low * low);
    }
    float sunWidth = sqrt(1.0 - sunDirection.y * sunDirection.y);
    float sunX = bearing * sunWidth;
    float sunY = (elevation - sunDirection.y) / sunWidth;
    float sunDistance = sqrt(sunX * sunX + sunY * sunY);
    // A warm, crisp disc with a quiet halo, anchored to the lighting direction.
    color = skyMix(color, sunHalo, haloStrength * (1.0 - clamp(sunDistance / haloRadius, 0.0, 1.0)));
    color = skyMix(color, sunDisc, discStrength * clamp((0.047 - sunDistance) / 0.004, 0.0, 1.0));
    if (elevation > 0.075) {
        vec4 flags = texelFetch(skyMask, ivec2(sx, sy), 0);
        bool underside = flags.r > 0.5, rim = flags.g > 0.5;
        if (underside || rim) {
            vec3 cloud = underside ? cloudUnderside : cloudRim;
            color = skyMix(color, cloud, (elevation - 0.075) / 0.10);
        }
    }
    // Subtle ordered grain at panorama resolution, fixed to the sky itself.
    float grain = ((sx & 1) == (sy & 1)) ? 0.6 : -0.6;
    vec3 sky = floor(clamp(color + grain, 0.0, 255.0) + 0.5) / 255.0;
    finalColor = vec4(squallSky(sky, ray), 1.0);
}
