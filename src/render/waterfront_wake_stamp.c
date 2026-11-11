#include "waterfront_wake_stamp.h"

#include "raymath.h"
#include "rlgl.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    STAMP_MODE,
    STAMP_PROBE_VALUE,
    STAMP_PROBE_FIELD,
    STAMP_PLAYERS,
    STAMP_LIFE,
    STAMP_TRAFFIC_LIFE,
    STAMP_RECORDS
};

static RenderTexture2D load_target(int width, int height, int format)
{
    RenderTexture2D target = { .id = rlLoadFramebuffer() };
    if (target.id == 0)
        return target;
    target.texture
        = (Texture2D) { .width = width, .height = height, .mipmaps = 1, .format = format };
    target.texture.id = rlLoadTexture(NULL, width, height, format, 1);
    if (target.texture.id != 0) {
        rlFramebufferAttach(
            target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
        if (rlFramebufferComplete(target.id))
            return target;
    }
    UnloadRenderTexture(target);
    return (RenderTexture2D) { 0 };
}

enum { RECORDS_SLOT = 14, PROBE_SLOT = 15 };

// Every mode declares both samplers, and drivers validate an unused sampler too: give each a
// texture, raylib's default where the mode reads none.
static void bind_samplers(unsigned int records, unsigned int probe_field)
{
    rlActiveTextureSlot(RECORDS_SLOT);
    rlEnableTexture(records ? records : rlGetTextureIdDefault());
    rlActiveTextureSlot(PROBE_SLOT);
    rlEnableTexture(probe_field ? probe_field : rlGetTextureIdDefault());
    rlActiveTextureSlot(0);
}

static void unbind_samplers(void)
{
    rlActiveTextureSlot(RECORDS_SLOT);
    rlDisableTexture();
    rlActiveTextureSlot(PROBE_SLOT);
    rlDisableTexture();
    rlActiveTextureSlot(0);
}

// Draw a full-target quad with the stamp shader in MODE, over the pixel rectangle of TARGET,
// with PROBE_FIELD (or none) bound for the probe comparison.
static void draw_full(
    WaterfrontWakeStamp *stamp, RenderTexture2D target, int mode, unsigned int probe_field)
{
    int width = target.texture.width, height = target.texture.height;
    BeginTextureMode(target);
    rlSetMatrixProjection(MatrixOrtho(0, width, 0, height, -10, 10));
    rlSetMatrixModelview(MatrixIdentity());
    rlDisableBackfaceCulling();
    BeginShaderMode(stamp->shader);
    SetShaderValue(stamp->shader, stamp->locations[STAMP_MODE], &mode, SHADER_UNIFORM_INT);
    bind_samplers(0, probe_field);
    rlBegin(RL_QUADS);
    rlColor4ub(0, 0, 0, 255);
    rlTexCoord2f(0, 0);
    rlVertex2f(0, 0);
    rlVertex2f(0, (float)height);
    rlVertex2f((float)width, (float)height);
    rlVertex2f((float)width, 0);
    rlEnd();
    rlDrawRenderBatchActive();
    EndShaderMode();
    unbind_samplers();
    rlEnableBackfaceCulling();
    EndTextureMode();
}

// Render-target completeness does not prove that float blending works: add a signed value
// twice into a 1x1 field, then compare it on the GPU and read back only the 8-bit verdict.
static bool probe(WaterfrontWakeStamp *stamp)
{
    RenderTexture2D field = load_target(1, 1, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
    RenderTexture2D verdict = load_target(1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    bool passed = false;
    if (field.id && verdict.id) {
        float value[] = { -0.75f, 0.5f, 1.25f, 0.125f }, sum[4];
        for (int i = 0; i < 4; ++i)
            sum[i] = 2 * value[i];
        BeginTextureMode(field);
        rlClearColor(0, 0, 0, 0);
        rlClearScreenBuffers();
        EndTextureMode();
        SetShaderValue(
            stamp->shader, stamp->locations[STAMP_PROBE_VALUE], value, SHADER_UNIFORM_VEC4);
        rlSetBlendFactors(RL_ONE, RL_ONE, RL_FUNC_ADD);
        BeginBlendMode(BLEND_CUSTOM);
        draw_full(stamp, field, 1, 0);
        draw_full(stamp, field, 1, 0);
        EndBlendMode();
        SetShaderValue(
            stamp->shader, stamp->locations[STAMP_PROBE_VALUE], sum, SHADER_UNIFORM_VEC4);
        draw_full(stamp, verdict, 2, field.texture.id);
        Image image = LoadImageFromTexture(verdict.texture);
        passed = image.data && ((unsigned char *)image.data)[0] > 128;
        UnloadImage(image);
    }
    if (field.id)
        UnloadRenderTexture(field);
    if (verdict.id)
        UnloadRenderTexture(verdict);
    return passed;
}

bool waterfront_wake_stamp_init(
    WaterfrontWakeStamp *stamp, const char *shader_directory, int size, float spacing)
{
    *stamp = (WaterfrontWakeStamp) { .size = size, .spacing = spacing };
    char vertex[1024], fragment[1024];
    if (snprintf(vertex, sizeof(vertex), "%s/waterfront_wake_stamp.vs", shader_directory)
            >= (int)sizeof(vertex)
        || snprintf(fragment, sizeof(fragment), "%s/waterfront_wake_stamp.fs", shader_directory)
            >= (int)sizeof(fragment))
        return false;
    stamp->shader = LoadShader(vertex, fragment);
    if (!IsShaderValid(stamp->shader) || stamp->shader.id == rlGetShaderIdDefault()) {
        stamp->shader = (Shader) { 0 };
        return false;
    }
    const char *names[] = { "stampMode", "probeValue", "probeField", "objectWakePlayerCount",
        "objectWakeLife", "objectWakeTrafficLife", "objectWakeData" };
    for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); ++i)
        stamp->locations[i] = GetShaderLocation(stamp->shader, names[i]);
    int probe_slot = PROBE_SLOT, records_slot = RECORDS_SLOT;
    rlEnableShader(stamp->shader.id);
    rlSetUniform(stamp->locations[STAMP_PROBE_FIELD], &probe_slot, SHADER_UNIFORM_INT, 1);
    rlSetUniform(stamp->locations[STAMP_RECORDS], &records_slot, SHADER_UNIFORM_INT, 1);
    rlDisableShader();
    stamp->blending = probe(stamp);
    if (waterfront_wake_stamp_resize(stamp, size, spacing))
        TraceLog(LOG_INFO, "Wake stamp: %dx%d RGBA16F field of %.2f m texels", size, size,
            (double)spacing);
    else
        TraceLog(LOG_INFO, "Wake stamp: float blending unavailable; using analytic wake shading");
    return true;
}

bool waterfront_wake_stamp_resize(WaterfrontWakeStamp *stamp, int size, float spacing)
{
    if (stamp->target.id)
        UnloadRenderTexture(stamp->target);
    stamp->target = (RenderTexture2D) { 0 };
    stamp->size = size;
    stamp->spacing = spacing;
    stamp->valid = stamp->supported = false;
    if (!stamp->blending || size < 2 || spacing <= 0)
        return false;
    stamp->target = load_target(size, size, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
    if (!stamp->target.id)
        return false;
    SetTextureFilter(stamp->target.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(stamp->target.texture, TEXTURE_WRAP_CLAMP);
    stamp->supported = true;
    return true;
}

void waterfront_wake_stamp_destroy(WaterfrontWakeStamp *stamp)
{
    if (stamp->target.id)
        UnloadRenderTexture(stamp->target);
    if (stamp->shader.id)
        UnloadShader(stamp->shader);
    *stamp = (WaterfrontWakeStamp) { 0 };
}

static void add_quad(WaterfrontWakeStamp *stamp, int piece, Vector2 centre, Vector2 direction,
    Vector2 normal, float u, float v0, float v1)
{
    WaterfrontWakeStampQuad *quad = &stamp->quads[stamp->quad_count++];
    float us[] = { -u, -u, u, u }, vs[] = { v0, v1, v1, v0 };
    for (int i = 0; i < 4; ++i)
        quad->corners[i] = Vector2Add(
            centre, Vector2Add(Vector2Scale(direction, us[i]), Vector2Scale(normal, vs[i])));
    quad->piece = piece;
    stamp->texels += (size_t)(2 * u * (v1 - v0) / (stamp->spacing * stamp->spacing));
}

// FNV-1a over everything a build reads, so identical records keep the built field.
static uint64_t records_key(const float *origins, const float *parameters, int count, int players,
    float life, float traffic_life, int origin_x, int origin_z)
{
    uint64_t key = 14695981039346656037u;
    const float *arrays[] = { origins, parameters };
    float scalars[]
        = { (float)count, (float)players, life, traffic_life, (float)origin_x, (float)origin_z };
    for (int k = 0; k < 3; ++k) {
        const unsigned char *bytes
            = k < 2 ? (const unsigned char *)arrays[k] : (const unsigned char *)scalars;
        size_t size = k < 2 ? (size_t)count * 4 * sizeof(float) : sizeof(scalars);
        for (size_t i = 0; i < size; ++i)
            key = (key ^ bytes[i]) * 1099511628211u;
    }
    return key;
}

int waterfront_wake_stamp_place(WaterfrontWakeStamp *stamp, Vector2 centre, const float *origins,
    const float *parameters, int count, int players, float life, float traffic_life, float *outside)
{
    int origin_x = (int)floorf(centre.x / stamp->spacing) - stamp->size / 2;
    int origin_z = (int)floorf(centre.y / stamp->spacing) - stamp->size / 2;
    stamp->placed_key
        = records_key(origins, parameters, count, players, life, traffic_life, origin_x, origin_z);
    stamp->origin_x = origin_x;
    stamp->origin_z = origin_z;
    stamp->quad_count = stamp->pieces = stamp->outside = 0;
    stamp->texels = 0;
    // Bilinear samples need both neighbouring texel centres inside the field.
    float lo_x = (origin_x + .5f) * stamp->spacing,
          hi_x = lo_x + (stamp->size - 1) * stamp->spacing;
    float lo_z = (origin_z + .5f) * stamp->spacing,
          hi_z = lo_z + (stamp->size - 1) * stamp->spacing;
    for (int i = 0; i < count && i < WATERFRONT_WAKE_STAMP_PIECES; ++i) {
        const float *a = origins + 4 * i, *b = parameters + 4 * i;
        float piece_life = i >= players && traffic_life > 0 ? traffic_life : life;
        // The piece is zero before its birth ramp, before rising and once fully faded, and
        // nonzero only where q = radius - 0.9 - 2.4 age lies in (-7, 2) and |u| < length.
        float span = b[0] * b[3];
        float young = fmaxf(b[1] - span, 0), old = fminf(b[1] + span, piece_life - span);
        if (b[1] <= 0 || young >= old || b[2] == 0)
            continue;
        float inner = .9f + 2.4f * young - 7, outer = .9f + 2.4f * old + 2;
        float v0 = inner > .5f ? sqrtf(inner * inner - .25f) : 0;
        Vector2 middle = { a[0], a[1] }, direction = { a[2], a[3] };
        Vector2 normal = { -direction.y, direction.x };
        int first = stamp->quad_count;
        if (v0 > 0) {
            add_quad(stamp, i, middle, direction, normal, b[0], v0, outer);
            add_quad(stamp, i, middle, direction, normal, b[0], -outer, -v0);
        } else {
            add_quad(stamp, i, middle, direction, normal, b[0], -outer, outer);
        }
        ++stamp->pieces;
        bool reaches = false;
        for (int q = first; q < stamp->quad_count; ++q)
            for (int c = 0; c < 4; ++c) {
                Vector2 p = stamp->quads[q].corners[c];
                reaches |= p.x < lo_x || p.x > hi_x || p.y < lo_z || p.y > hi_z;
            }
        if (reaches)
            outside[4 * stamp->outside++] = (float)i;
    }
    return stamp->outside;
}

void waterfront_wake_stamp_build(
    WaterfrontWakeStamp *stamp, Texture2D records, int players, float life, float traffic_life)
{
    if (!stamp->supported || (stamp->valid && stamp->built_key == stamp->placed_key))
        return;
    BeginTextureMode(stamp->target);
    rlClearColor(0, 0, 0, 0);
    rlClearScreenBuffers();
    if (stamp->quad_count) {
        rlSetMatrixProjection(MatrixOrtho(0, stamp->size, 0, stamp->size, -10, 10));
        rlSetMatrixModelview(MatrixIdentity());
        rlDisableDepthTest();
        rlDisableBackfaceCulling();
        Shader shader = stamp->shader;
        BeginShaderMode(shader);
        int mode = 0;
        SetShaderValue(shader, stamp->locations[STAMP_MODE], &mode, SHADER_UNIFORM_INT);
        SetShaderValue(shader, stamp->locations[STAMP_PLAYERS], &players, SHADER_UNIFORM_INT);
        SetShaderValue(shader, stamp->locations[STAMP_LIFE], &life, SHADER_UNIFORM_FLOAT);
        SetShaderValue(
            shader, stamp->locations[STAMP_TRAFFIC_LIFE], &traffic_life, SHADER_UNIFORM_FLOAT);
        bind_samplers(records.id, 0);
        rlSetBlendFactors(RL_ONE, RL_ONE, RL_FUNC_ADD);
        BeginBlendMode(BLEND_CUSTOM);
        float scale = 1 / stamp->spacing;
        rlBegin(RL_QUADS);
        for (int q = 0; q < stamp->quad_count; ++q) {
            const WaterfrontWakeStampQuad *quad = &stamp->quads[q];
            rlColor4ub(
                (unsigned char)(quad->piece & 255), (unsigned char)(quad->piece >> 8), 0, 255);
            for (int c = 0; c < 4; ++c) {
                Vector2 p = quad->corners[c];
                rlTexCoord2f(p.x, p.y);
                rlVertex2f(p.x * scale - stamp->origin_x, p.y * scale - stamp->origin_z);
            }
        }
        rlEnd();
        rlDrawRenderBatchActive();
        EndBlendMode();
        EndShaderMode();
        unbind_samplers();
        rlEnableBackfaceCulling();
    }
    EndTextureMode();
    stamp->valid = true;
    stamp->built_key = stamp->placed_key;
    ++stamp->builds;
}

void waterfront_wake_stamp_rect(const WaterfrontWakeStamp *stamp, float rect[4])
{
    float extent = stamp->size * stamp->spacing;
    rect[0] = stamp->origin_x * stamp->spacing;
    rect[1] = stamp->origin_z * stamp->spacing;
    rect[2] = 1 / extent;
    rect[3] = .5f / stamp->size;
}

size_t waterfront_wake_stamp_bytes(const WaterfrontWakeStamp *stamp)
{
    return stamp->target.id ? (size_t)stamp->size * stamp->size * 8 : 0;
}
