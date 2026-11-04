#version 330

// G5a presentation of the HDR scene into the 8-bit target. Bloom blurs the light above the
// grade's threshold through a short dual-filter chain at half, quarter and eighth size; the
// composite scales light above 1 back to the clamp with its hue, adds the bloom, applies the
// compact grade and clamps. There is no other tone curve.
in vec2 fragTexCoord;
uniform sampler2D texture0; // This pass's source.
uniform sampler2D bloomTexture; // Composite: the half-size bloom level.
uniform int postMode; // 0 bright pass and downsample, 1 downsample, 2 upsample, 3 composite.
uniform vec2 sourceTexel; // One texel of texture0 in UV.
uniform vec3 bloomParameters; // Threshold, knee, strength.
uniform vec3 gradeParameters; // Exposure, contrast, saturation.
uniform vec3 gradeLift;
uniform vec3 gradeGain;
out vec4 finalColor;

// Only light above the threshold blooms. Its excess eases in quadratically over the knee and
// then grows linearly, so a value at the threshold adds nothing.
vec3 bright(vec3 color)
{
    float knee = max(bloomParameters.y, 1e-4);
    float peak = max(color.r, max(color.g, color.b));
    float excess = max(peak - bloomParameters.x, 0.0);
    float weight = excess < knee ? excess * excess / (2.0 * knee) : excess - 0.5 * knee;
    return color * weight / max(peak, 1e-4);
}

// Four bilinear taps around the centre cover a 4x4 source block; the centre weighs half.
vec3 down(vec2 uv)
{
    vec2 d = sourceTexel;
    vec3 sum = texture(texture0, uv).rgb * 4.0;
    sum += texture(texture0, uv + vec2(-d.x, -d.y)).rgb;
    sum += texture(texture0, uv + vec2(d.x, -d.y)).rgb;
    sum += texture(texture0, uv + vec2(-d.x, d.y)).rgb;
    sum += texture(texture0, uv + vec2(d.x, d.y)).rgb;
    return sum / 8.0;
}

vec3 up(vec2 uv)
{
    vec2 d = sourceTexel;
    vec3 sum = texture(texture0, uv + vec2(-2.0 * d.x, 0.0)).rgb;
    sum += texture(texture0, uv + vec2(2.0 * d.x, 0.0)).rgb;
    sum += texture(texture0, uv + vec2(0.0, -2.0 * d.y)).rgb;
    sum += texture(texture0, uv + vec2(0.0, 2.0 * d.y)).rgb;
    sum += texture(texture0, uv + vec2(-d.x, -d.y)).rgb * 2.0;
    sum += texture(texture0, uv + vec2(d.x, -d.y)).rgb * 2.0;
    sum += texture(texture0, uv + vec2(-d.x, d.y)).rgb * 2.0;
    sum += texture(texture0, uv + vec2(d.x, d.y)).rgb * 2.0;
    return sum / 12.0;
}

void main()
{
    if (postMode == 0) {
        vec2 d = sourceTexel;
        vec3 sum = bright(texture(texture0, fragTexCoord).rgb) * 4.0;
        sum += bright(texture(texture0, fragTexCoord + vec2(-d.x, -d.y)).rgb);
        sum += bright(texture(texture0, fragTexCoord + vec2(d.x, -d.y)).rgb);
        sum += bright(texture(texture0, fragTexCoord + vec2(-d.x, d.y)).rgb);
        sum += bright(texture(texture0, fragTexCoord + vec2(d.x, d.y)).rgb);
        finalColor = vec4(sum / 8.0, 1.0);
        return;
    }
    if (postMode == 1) {
        finalColor = vec4(down(fragTexCoord), 1.0);
        return;
    }
    if (postMode == 2) {
        finalColor = vec4(up(fragTexCoord), 1.0);
        return;
    }
    // Light above 1 keeps its hue: scale it back to the clamp instead of clipping each channel,
    // so a warm lamp stays warm and only its bloom shows the excess. Colours within 1 pass as
    // they are.
    vec3 color = texture(texture0, fragTexCoord).rgb;
    color /= max(1.0, max(color.r, max(color.g, color.b)));
    color += texture(bloomTexture, fragTexCoord).rgb * bloomParameters.z;
    color *= gradeParameters.x;
    color = color * gradeGain + gradeLift * max(1.0 - color, 0.0);
    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(luma), color, gradeParameters.z);
    color = (color - 0.5) * gradeParameters.y + 0.5;
    finalColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
