#include "waterfront_water_history.h"

#include "raymath.h"
#include "rlgl.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum {
    HISTORY_MODE,
    HISTORY_FILL,
    HISTORY_PREVIOUS,
    HISTORY_MASK,
    HISTORY_STAMP,
    HISTORY_STAMP_RECT,
    HISTORY_ORIGIN,
    HISTORY_SHIFT,
    HISTORY_STEP,
    HISTORY_FOAM,
    HISTORY_RIPPLE,
    HISTORY_SOURCE_COUNT,
    HISTORY_SOURCES,
    HISTORY_HULL_COUNT,
    HISTORY_HULLS,
    HISTORY_RAIN,
    HISTORY_RAIN_IMPULSE,
    HISTORY_LOCATIONS
};

enum { PREVIOUS_SLOT = 1, MASK_SLOT = 2, STAMP_SLOT = 3 };

WaterfrontWaterHistoryTuning waterfront_water_history_tuning(void)
{
    return (WaterfrontWaterHistoryTuning) {
        .foam_half_life = 1.1f,
        .foam_seed_gain = 1.6f,
        .foam_drift = { -0.15f, -0.2f },
        .ripple_speed = 3.0f,
        .ripple_half_life = 1.6f,
        .ripple_limit = 0.08f,
        .ripple_slope_gain = 4.0f,
        .bow_impulse = 0.0025f,
        .contact_impulse = 0.0012f,
        .rain_rate = 0.05f,
        .rain_impulse = -0.008f,
    };
}

static RenderTexture2D load_target(int size, int format)
{
    RenderTexture2D target = { .id = rlLoadFramebuffer() };
    if (target.id == 0)
        return target;
    target.texture = (Texture2D) { .width = size, .height = size, .mipmaps = 1, .format = format };
    target.texture.id = rlLoadTexture(NULL, size, size, format, 1);
    if (target.texture.id != 0) {
        rlFramebufferAttach(
            target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
        if (rlFramebufferComplete(target.id)) {
            SetTextureWrap(target.texture, TEXTURE_WRAP_CLAMP);
            return target;
        }
    }
    UnloadRenderTexture(target);
    return (RenderTexture2D) { 0 };
}

static void unload_targets(WaterfrontWaterHistory *history)
{
    for (int i = 0; i < 2; ++i)
        if (history->states[i].id)
            UnloadRenderTexture(history->states[i]);
    if (history->mask.id)
        UnloadRenderTexture(history->mask);
    history->states[0] = history->states[1] = history->mask = (RenderTexture2D) { 0 };
}

bool waterfront_water_history_init(
    WaterfrontWaterHistory *history, const char *shader_directory, bool float_targets)
{
    *history = (WaterfrontWaterHistory) { .tuning = waterfront_water_history_tuning() };
    char vertex[1024], fragment[1024];
    if (snprintf(vertex, sizeof(vertex), "%s/waterfront_water_history.vs", shader_directory)
            >= (int)sizeof(vertex)
        || snprintf(fragment, sizeof(fragment), "%s/waterfront_water_history.fs", shader_directory)
            >= (int)sizeof(fragment))
        return false;
    history->shader = LoadShader(vertex, fragment);
    if (!IsShaderValid(history->shader) || history->shader.id == rlGetShaderIdDefault()) {
        history->shader = (Shader) { 0 };
        return false;
    }
    const char *names[HISTORY_LOCATIONS] = { "historyMode", "fillValue", "previousState",
        "waterMask", "stampField", "stampRect", "historyOrigin", "historyShift", "historyStep",
        "foamCoefficients", "rippleCoefficients", "sourceCount", "sources[0]", "hullCount",
        "hulls[0]", "rain", "rainImpulse" };
    for (int i = 0; i < HISTORY_LOCATIONS; ++i)
        history->locations[i] = GetShaderLocation(history->shader, names[i]);
    int slots[] = { PREVIOUS_SLOT, MASK_SLOT, STAMP_SLOT };
    rlEnableShader(history->shader.id);
    for (int i = 0; i < 3; ++i)
        rlSetUniform(history->locations[HISTORY_PREVIOUS + i], &slots[i], SHADER_UNIFORM_INT, 1);
    rlDisableShader();
    history->material = LoadMaterialDefault();
    history->material.shader = history->shader;
    if (!float_targets) {
        TraceLog(LOG_INFO, "Water history: float targets unavailable; no foam history");
        return true;
    }
    int size = WATERFRONT_WATER_HISTORY_SIZE;
    history->states[0] = load_target(size, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
    history->states[1] = load_target(size, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
    // RGBA8: raylib's GLES grayscale is luminance, which WebGL2 cannot render into.
    history->mask = load_target(size, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    if (!history->states[0].id || !history->states[1].id || !history->mask.id) {
        unload_targets(history);
        TraceLog(LOG_INFO, "Water history: targets unavailable; no foam history");
        return true;
    }
    for (int i = 0; i < 2; ++i)
        SetTextureFilter(history->states[i].texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(history->mask.texture, TEXTURE_FILTER_POINT);
    history->supported = true;
    TraceLog(LOG_INFO, "Water history: %dx%d RGBA16F foam and ripples of %.2f m texels", size, size,
        (double)WATERFRONT_WATER_HISTORY_SPACING);
    return true;
}

void waterfront_water_history_destroy(WaterfrontWaterHistory *history)
{
    unload_targets(history);
    if (history->shader.id) {
        // The material borrows the shader; free only its map array.
        history->material.shader = (Shader) { 0 };
        UnloadShader(history->shader);
    }
    if (history->material.maps)
        MemFree(history->material.maps);
    *history = (WaterfrontWaterHistory) { 0 };
}

void waterfront_water_history_invalidate(WaterfrontWaterHistory *history)
{
    history->valid = false;
}

// The shader declares every sampler in both modes, and drivers validate unused samplers too:
// give each slot a texture, raylib's default where there is none.
static void bind_samplers(unsigned int previous, unsigned int mask, unsigned int stamp)
{
    unsigned int textures[] = { previous, mask, stamp };
    for (int i = 0; i < 3; ++i) {
        rlActiveTextureSlot(PREVIOUS_SLOT + i);
        rlEnableTexture(textures[i] ? textures[i] : rlGetTextureIdDefault());
    }
    rlActiveTextureSlot(0);
}

static void unbind_samplers(void)
{
    for (int i = 0; i < 3; ++i) {
        rlActiveTextureSlot(PREVIOUS_SLOT + i);
        rlDisableTexture();
    }
    rlActiveTextureSlot(0);
}

static void set_mode(WaterfrontWaterHistory *history, int mode, Vector4 fill)
{
    SetShaderValue(history->shader, history->locations[HISTORY_MODE], &mode, SHADER_UNIFORM_INT);
    SetShaderValue(history->shader, history->locations[HISTORY_FILL], &fill, SHADER_UNIFORM_VEC4);
}

static void draw_quad(int size)
{
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    rlTexCoord2f(0, 0);
    rlVertex2f(0, 0);
    rlVertex2f(0, (float)size);
    rlVertex2f((float)size, (float)size);
    rlVertex2f((float)size, 0);
    rlEnd();
    rlDrawRenderBatchActive();
}

// Begin drawing into TARGET with a pixel-space orthographic projection.
static void begin_target(RenderTexture2D target)
{
    BeginTextureMode(target);
    rlSetMatrixProjection(MatrixOrtho(0, target.texture.width, 0, target.texture.height, -10, 10));
    rlSetMatrixModelview(MatrixIdentity());
    rlDisableDepthTest();
    rlDisableBackfaceCulling();
    rlDisableColorBlend();
}

static void end_target(void)
{
    rlEnableColorBlend();
    rlEnableBackfaceCulling();
    rlEnableDepthTest();
    EndTextureMode();
}

static void clear_states(WaterfrontWaterHistory *history)
{
    for (int i = 0; i < 2; ++i) {
        BeginTextureMode(history->states[i]);
        rlClearColor(0, 0, 0, 0);
        rlClearScreenBuffers();
        EndTextureMode();
    }
}

// Mark water texels from the water surface seen straight down over the field; without a
// surface every texel is open.
static void draw_mask(WaterfrontWaterHistory *history, const Mesh *water)
{
    float spacing = WATERFRONT_WATER_HISTORY_SPACING;
    float extent = WATERFRONT_WATER_HISTORY_SIZE * spacing;
    float x0 = history->origin_x * spacing, z0 = history->origin_z * spacing;
    BeginTextureMode(history->mask);
    unsigned char open = water && water->vaoId ? 0 : 255;
    rlClearColor(open, open, open, 255);
    rlClearScreenBuffers();
    if (!open) {
        // Rotating -90 degrees about X maps world (x, y, z) to (x, z, -y): Z becomes the
        // target's rows, as in the step's texel addressing.
        rlSetMatrixProjection(MatrixOrtho(x0, x0 + extent, z0, z0 + extent, -1000, 1000));
        rlSetMatrixModelview(MatrixRotateX(-PI / 2));
        rlDisableDepthTest();
        rlDisableBackfaceCulling();
        rlDisableColorBlend();
        BeginShaderMode(history->shader);
        set_mode(history, 1, (Vector4) { 1, 1, 1, 1 });
        bind_samplers(0, 0, 0);
        DrawMesh(*water, history->material, MatrixIdentity());
        unbind_samplers();
        EndShaderMode();
        rlEnableColorBlend();
        rlEnableBackfaceCulling();
        rlEnableDepthTest();
    }
    EndTextureMode();
    history->mask_x = history->origin_x;
    history->mask_z = history->origin_z;
    ++history->masks;
}

static void place_origin(WaterfrontWaterHistory *history, Vector2 centre)
{
    int block = WATERFRONT_WATER_HISTORY_BLOCK, half = WATERFRONT_WATER_HISTORY_SIZE / 2;
    float spacing = WATERFRONT_WATER_HISTORY_SPACING;
    history->origin_x = (int)floorf(centre.x / (spacing * block) + .5f) * block - half;
    history->origin_z = (int)floorf(centre.y / (spacing * block) + .5f) * block - half;
}

// One fixed substep from the current state into the other, with SHIFT texels of origin
// movement since the previous substep and SOURCES ripple impulses.
static void substep(WaterfrontWaterHistory *history, const WaterfrontWaterHistoryInput *input,
    int shift_x, int shift_z, const Vector4 *sources, int source_count)
{
    const WaterfrontWaterHistoryTuning *t = &history->tuning;
    float dt = 1.0f / WATERFRONT_WATER_HISTORY_HZ, spacing = WATERFRONT_WATER_HISTORY_SPACING;
    int size = WATERFRONT_WATER_HISTORY_SIZE, next = 1 - history->current;
    Shader shader = history->shader;
    const int *loc = history->locations;
    begin_target(history->states[next]);
    BeginShaderMode(shader);
    set_mode(history, 0, (Vector4) { 0 });
    float stamp_rect[4] = { 0 };
    if (input->stamp)
        memcpy(stamp_rect, input->stamp_rect, sizeof(stamp_rect));
    SetShaderValue(shader, loc[HISTORY_STAMP_RECT], stamp_rect, SHADER_UNIFORM_VEC4);
    float origin[] = { history->origin_x * spacing, history->origin_z * spacing, spacing };
    SetShaderValue(shader, loc[HISTORY_ORIGIN], origin, SHADER_UNIFORM_VEC3);
    int shift[] = { shift_x, shift_z };
    SetShaderValue(shader, loc[HISTORY_SHIFT], shift, SHADER_UNIFORM_IVEC2);
    int step = (int)(history->step & 0x7fffffff);
    SetShaderValue(shader, loc[HISTORY_STEP], &step, SHADER_UNIFORM_INT);
    float foam[] = { powf(.5f, dt / fmaxf(t->foam_half_life, dt)), t->foam_seed_gain,
        t->foam_drift.x * dt / spacing, t->foam_drift.y * dt / spacing };
    SetShaderValue(shader, loc[HISTORY_FOAM], foam, SHADER_UNIFORM_VEC4);
    float courant = t->ripple_speed * dt / spacing;
    // Energy halves over the half-life; amplitude decays by the square root of that.
    float ripple[] = { courant * courant, powf(.5f, .5f * dt / fmaxf(t->ripple_half_life, dt)),
        t->ripple_limit, 0 };
    SetShaderValue(shader, loc[HISTORY_RIPPLE], ripple, SHADER_UNIFORM_VEC4);
    SetShaderValue(shader, loc[HISTORY_SOURCE_COUNT], &source_count, SHADER_UNIFORM_INT);
    if (source_count)
        SetShaderValueV(shader, loc[HISTORY_SOURCES], sources, SHADER_UNIFORM_VEC4, source_count);
    Vector4 hulls[WATERFRONT_WATER_HISTORY_HULLS];
    for (int i = 0; i < input->hull_count; ++i)
        hulls[i] = (Vector4) { input->hulls[i].x, input->hulls[i].y, input->hulls[i].z, 0 };
    SetShaderValue(shader, loc[HISTORY_HULL_COUNT], &input->hull_count, SHADER_UNIFORM_INT);
    if (input->hull_count)
        SetShaderValueV(shader, loc[HISTORY_HULLS], hulls, SHADER_UNIFORM_VEC4, input->hull_count);
    // A drop lands on a texel with this chance each substep.
    float chance = Clamp(input->rain.w, 0, 1) * t->rain_rate * spacing * spacing * dt;
    float rain[] = { input->rain.x, input->rain.y, input->rain.z, input->rain.w > 0 ? chance : 0 };
    SetShaderValue(shader, loc[HISTORY_RAIN], rain, SHADER_UNIFORM_VEC4);
    SetShaderValue(shader, loc[HISTORY_RAIN_IMPULSE], &t->rain_impulse, SHADER_UNIFORM_FLOAT);
    bind_samplers(
        history->states[history->current].texture.id, history->mask.texture.id, input->stamp);
    draw_quad(size);
    unbind_samplers();
    EndShaderMode();
    end_target();
    history->current = next;
    ++history->substeps;
}

// Ripple impulses for the substep STEP: water pushed aside at both shoulders of each moving
// hull (ahead of it the hull's own disc would erase them) and a slow bob at each contact.
static int gather_sources(const WaterfrontWaterHistory *history,
    const WaterfrontWaterHistoryInput *input, const Vector2 *velocities, long long step,
    Vector4 *sources)
{
    const WaterfrontWaterHistoryTuning *t = &history->tuning;
    int count = 0;
    for (int i = 0; i < input->hull_count && count + 2 <= WATERFRONT_WATER_HISTORY_SOURCES; ++i) {
        float speed = Vector2Length(velocities[i]);
        if (speed < .5f)
            continue;
        Vector2 forward = Vector2Scale(velocities[i], 1 / speed);
        Vector2 side = { -forward.y, forward.x };
        // Pulse with the distance run along the heading, so the trail is wavelets rather than
        // one ridge; the phase depends only on where the hull is.
        float run = input->hulls[i].x * forward.x + input->hulls[i].y * forward.y;
        float impulse = t->bow_impulse * fminf(speed / 6, 1) * sinf(2 * PI * run / 6);
        float reach = input->hulls[i].z + .5f;
        for (int s = -1; s <= 1; s += 2) {
            Vector2 at = Vector2Add(Vector2Scale(side, s * reach), Vector2Scale(forward, .5f));
            sources[count++]
                = (Vector4) { input->hulls[i].x + at.x, input->hulls[i].y + at.y, .9f, impulse };
        }
    }
    for (int i = 0; i < input->contact_count && count < WATERFRONT_WATER_HISTORY_SOURCES; ++i) {
        float phase = 2 * PI * (float)((step + 7 * i) % 45) / 45;
        sources[count++] = (Vector4) { input->contacts[i].x, input->contacts[i].y, .6f,
            t->contact_impulse * sinf(phase) };
    }
    return count;
}

int waterfront_water_history_advance(
    WaterfrontWaterHistory *history, const WaterfrontWaterHistoryInput *input)
{
    history->last_substeps = 0;
    if (!history->supported)
        return 0;
    const int hz = WATERFRONT_WATER_HISTORY_HZ;
    // Game clocks land exactly on substep boundaries; keep rounding from splitting them.
    long long target = (long long)floor(input->time * hz + 1e-4);
    int hulls = input->hull_count < WATERFRONT_WATER_HISTORY_HULLS ? input->hull_count
                                                                   : WATERFRONT_WATER_HISTORY_HULLS;
    WaterfrontWaterHistoryInput clamped = *input;
    clamped.hull_count = hulls < 0 ? 0 : hulls;
    if (clamped.contact_count > WATERFRONT_WATER_HISTORY_CONTACTS)
        clamped.contact_count = WATERFRONT_WATER_HISTORY_CONTACTS;
    bool reset = !history->valid || input->content != history->content || target < history->step
        || target - history->step > (long long)(WATERFRONT_WATER_HISTORY_JUMP * hz)
        || Vector2Distance(input->centre, history->centre) > WATERFRONT_WATER_HISTORY_TELEPORT
        || clamped.hull_count != history->hull_count;
    Vector2 velocities[WATERFRONT_WATER_HISTORY_HULLS] = { 0 };
    int steps;
    int shift_x = 0, shift_z = 0;
    if (reset) {
        history->valid = true;
        history->content = input->content;
        history->step = target - WATERFRONT_WATER_HISTORY_PREROLL;
        steps = WATERFRONT_WATER_HISTORY_PREROLL;
        place_origin(history, input->centre);
        clear_states(history);
        history->current = 0;
        draw_mask(history, input->water);
        ++history->resets;
    } else {
        long long behind = target - history->step;
        if (behind == 0)
            return 0;
        steps = behind < WATERFRONT_WATER_HISTORY_CATCH_UP ? (int)behind
                                                           : WATERFRONT_WATER_HISTORY_CATCH_UP;
        history->step = target - steps;
        double elapsed = input->time - history->previous_time;
        for (int i = 0; i < clamped.hull_count && elapsed > 0; ++i) {
            Vector2 now = { input->hulls[i].x, input->hulls[i].y };
            velocities[i] = Vector2Scale(
                Vector2Subtract(now, history->previous_hulls[i]), (float)(1 / elapsed));
        }
        int old_x = history->origin_x, old_z = history->origin_z;
        place_origin(history, input->centre);
        shift_x = history->origin_x - old_x;
        shift_z = history->origin_z - old_z;
        if (history->origin_x != history->mask_x || history->origin_z != history->mask_z)
            draw_mask(history, input->water);
    }
    history->centre = input->centre;
    history->hull_count = clamped.hull_count;
    for (int i = 0; i < clamped.hull_count; ++i)
        history->previous_hulls[i] = (Vector2) { input->hulls[i].x, input->hulls[i].y };
    history->previous_time = input->time;
    Vector4 sources[WATERFRONT_WATER_HISTORY_SOURCES];
    for (int s = 0; s < steps; ++s) {
        ++history->step;
        int count = gather_sources(history, &clamped, velocities, history->step, sources);
        substep(history, &clamped, s == 0 ? shift_x : 0, s == 0 ? shift_z : 0, sources, count);
    }
    history->last_substeps = steps;
    return steps;
}

void waterfront_water_history_rect(const WaterfrontWaterHistory *history, float rect[4])
{
    float spacing = WATERFRONT_WATER_HISTORY_SPACING;
    rect[0] = history->origin_x * spacing;
    rect[1] = history->origin_z * spacing;
    rect[2] = 1 / (WATERFRONT_WATER_HISTORY_SIZE * spacing);
    rect[3] = WATERFRONT_WATER_HISTORY_FADE;
}

Texture2D waterfront_water_history_texture(const WaterfrontWaterHistory *history)
{
    return history->states[history->current].texture;
}

size_t waterfront_water_history_bytes(const WaterfrontWaterHistory *history)
{
    size_t texels = (size_t)WATERFRONT_WATER_HISTORY_SIZE * WATERFRONT_WATER_HISTORY_SIZE;
    return history->supported ? texels * (2 * 8 + 4) : 0;
}
