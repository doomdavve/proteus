#version 330

in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec3 skyForward;
uniform vec3 skyRight;
uniform vec3 skyUp;
uniform vec2 skyScale;
uniform vec3 skyEye;
// A3.5 weather patch: centre X/Z, radius and grown fraction; base and top heights. Keep in
// step with waterfront_squall_sky (src/waterfront_scene.c).
uniform vec4 squallArea;
uniform vec2 squallHeight;
out vec4 finalColor;

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
    vec2 uv = vec2(fract(atan(ray.x, ray.z) / 6.28318530718 + 0.5),
        clamp((1.0 - ray.y) * 0.5, 0.0, 1.0));
    vec4 sky = texture(texture0, uv);
    finalColor = vec4(squallSky(sky.rgb, ray), sky.a);
}
