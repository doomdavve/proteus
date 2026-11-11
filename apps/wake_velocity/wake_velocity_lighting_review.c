// Developer-only live lighting-cycle audit. Native GLAD spies prove what the running game
// actually uploaded for the sky sun direction and sky mask texture; rows are buffered and one CSV
// is written at the end.
#ifndef __EMSCRIPTEN__

#include "wake_velocity/wake_velocity_lighting_review.h"
#include "external/glad.h"

#include "render/waterfront_lighting.h"
#include "rlgl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REVIEW_MAX_ROWS 18000

#define REVIEW_CSV_HEADER                                                                          \
    "time,serial,tick,clock_hours,canonical_hour,period,rate,cycle_enabled,active,focused,"        \
    "paused,frame_ms,interval_ms,render_ms,gpu_ms,has_gpu,clock_glitter_seconds,sun_seen,"         \
    "sun_x,sun_y,sun_z,cpu_light_x,cpu_light_y,cpu_light_z,sky_tex_image,sky_tex_sub_image,"       \
    "sky_texture_id\n"

typedef struct {
    double time; // Relative to the first recorded row.
    unsigned long long serial;
    unsigned long long tick;
    double clock_hours;
    double canonical_hour;
    double period;
    float rate;
    bool cycle_enabled;
    bool active, focused, paused;
    float frame_ms, interval_ms, render_ms;
    float gpu_ms;
    bool has_gpu;
    double glitter_seconds;
    bool has_glitter;
    bool sun_seen;
    Vector3 sun;
    bool has_cpu;
    Vector3 cpu_light;
    long long sky_tex_image;
    long long sky_tex_sub_image;
    unsigned long long sky_texture_id;
} ReviewRow;

typedef struct {
    FILE *file;
    ReviewRow *rows;
    size_t count;
    size_t capacity;
    WaterfrontGpuScene *scene;
    bool first_time_set;
    double first_time;
    bool overflow_warned;
    // Native GL spies. The real function pointers are restored on end or failed begin.
    bool spies_installed;
    PFNGLUNIFORM3FVPROC real_uniform3fv;
    PFNGLTEXIMAGE2DPROC real_tex_image;
    PFNGLTEXSUBIMAGE2DPROC real_tex_sub_image;
    GLuint spy_texture; // Initial sky mask, or the positive-control probe.
    GLint saved_texture_binding;
    bool binding_saved;
    GLuint sky_program;
    GLint sun_location;
    long long sky_tex_image_calls;
    long long sky_tex_sub_image_calls;
    Vector3 observed_sun;
    bool sun_pending;
} ReviewState;

// GLAD callbacks carry no user data; only one review may own the spies at a time.
static ReviewState *active_review;

static void GLAD_API_PTR review_uniform3fv(GLint location, GLsizei count, const GLfloat *value)
{
    ReviewState *review = active_review;
    if (review != NULL && value != NULL && count > 0 && location == review->sun_location) {
        GLint program = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        if ((GLuint)program == review->sky_program) {
            review->observed_sun = (Vector3) { value[0], value[1], value[2] };
            review->sun_pending = true;
        }
    }
    if (review != NULL && review->real_uniform3fv != NULL)
        review->real_uniform3fv(location, count, value);
}

static void GLAD_API_PTR review_tex_image(GLenum target, GLint level, GLint internalformat,
    GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels)
{
    ReviewState *review = active_review;
    if (review != NULL) {
        GLint bound = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
        if ((GLuint)bound == review->spy_texture)
            ++review->sky_tex_image_calls;
        review->real_tex_image(
            target, level, internalformat, width, height, border, format, type, pixels);
    }
}

static void GLAD_API_PTR review_tex_sub_image(GLenum target, GLint level, GLint xoffset,
    GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels)
{
    ReviewState *review = active_review;
    if (review != NULL) {
        GLint bound = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
        if ((GLuint)bound == review->spy_texture)
            ++review->sky_tex_sub_image_calls;
        review->real_tex_sub_image(
            target, level, xoffset, yoffset, width, height, format, type, pixels);
    }
}

static bool install_spies(ReviewState *review, GLuint texture)
{
    PFNGLUNIFORM3FVPROC uniform = glad_glUniform3fv;
    PFNGLTEXIMAGE2DPROC image = glad_glTexImage2D;
    PFNGLTEXSUBIMAGE2DPROC sub_image = glad_glTexSubImage2D;
    if (uniform == NULL || image == NULL || sub_image == NULL)
        return false;

    GLint binding = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    review->real_uniform3fv = uniform;
    review->real_tex_image = image;
    review->real_tex_sub_image = sub_image;
    review->spy_texture = texture;
    review->saved_texture_binding = binding;
    review->binding_saved = true;
    review->sky_tex_image_calls = 0;
    review->sky_tex_sub_image_calls = 0;
    active_review = review;
    glad_glUniform3fv = review_uniform3fv;
    glad_glTexImage2D = review_tex_image;
    glad_glTexSubImage2D = review_tex_sub_image;
    review->spies_installed = true;
    return true;
}

static void restore_spies(ReviewState *review)
{
    if (!review->spies_installed)
        return;
    if (review->real_uniform3fv != NULL)
        glad_glUniform3fv = review->real_uniform3fv;
    if (review->real_tex_image != NULL)
        glad_glTexImage2D = review->real_tex_image;
    if (review->real_tex_sub_image != NULL)
        glad_glTexSubImage2D = review->real_tex_sub_image;
    if (review->binding_saved)
        glBindTexture(GL_TEXTURE_2D, (GLuint)review->saved_texture_binding);
    if (active_review == review)
        active_review = NULL;
    review->spies_installed = false;
    review->real_uniform3fv = NULL;
    review->real_tex_image = NULL;
    review->real_tex_sub_image = NULL;
    review->binding_saved = false;
}

// A 4x4 probe proves both texture spy paths before the real sky mask is watched.
static bool spy_positive_control(ReviewState *review)
{
    GLint original_binding = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &original_binding);

    Color pixels[16] = { 0 };
    Image image = { .data = pixels,
        .width = 4,
        .height = 4,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    Texture2D probe = LoadTextureFromImage(image);
    if (!IsTextureValid(probe)) {
        if (probe.id != 0)
            UnloadTexture(probe);
        glBindTexture(GL_TEXTURE_2D, (GLuint)original_binding);
        return false;
    }

    bool ok = false;
    if (install_spies(review, probe.id)) {
        UpdateTexture(probe, pixels);
        long long update_image = review->sky_tex_image_calls;
        long long update_sub_image = review->sky_tex_sub_image_calls;
        glBindTexture(GL_TEXTURE_2D, probe.id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        long long alloc_image = review->sky_tex_image_calls;
        long long alloc_sub_image = review->sky_tex_sub_image_calls;
        restore_spies(review);
        ok = update_image == 0 && update_sub_image == 1 && alloc_image == 1 && alloc_sub_image == 1;
    }

    glBindTexture(GL_TEXTURE_2D, (GLuint)original_binding);
    if (probe.id != 0)
        UnloadTexture(probe);
    return ok;
}

static void cell_double(FILE *file, double value)
{
    if (isfinite(value))
        fprintf(file, "%.17g", value);
}

static void cell_float9(FILE *file, float value)
{
    if (isfinite(value))
        fprintf(file, "%.9g", value);
}

static bool write_row(FILE *file, const ReviewRow *row)
{
    cell_double(file, row->time);
    fprintf(file, ",%llu,%llu,", row->serial, row->tick);
    cell_double(file, row->clock_hours);
    fputc(',', file);
    cell_double(file, row->canonical_hour);
    fputc(',', file);
    cell_double(file, row->period);
    fputc(',', file);
    cell_float9(file, row->rate);
    fprintf(file, ",%d,%d,%d,%d,", row->cycle_enabled ? 1 : 0, row->active ? 1 : 0,
        row->focused ? 1 : 0, row->paused ? 1 : 0);
    cell_float9(file, row->frame_ms);
    fputc(',', file);
    cell_float9(file, row->interval_ms);
    fputc(',', file);
    cell_float9(file, row->render_ms);
    fputc(',', file);
    if (row->has_gpu)
        cell_float9(file, row->gpu_ms);
    fprintf(file, ",%d,", row->has_gpu ? 1 : 0);
    if (row->has_glitter)
        cell_double(file, row->glitter_seconds);
    fprintf(file, ",%d,", row->sun_seen ? 1 : 0);
    if (row->sun_seen) {
        cell_float9(file, row->sun.x);
        fputc(',', file);
        cell_float9(file, row->sun.y);
        fputc(',', file);
        cell_float9(file, row->sun.z);
    } else {
        fputs(",,", file);
    }
    fputc(',', file);
    if (row->has_cpu) {
        cell_float9(file, row->cpu_light.x);
        fputc(',', file);
        cell_float9(file, row->cpu_light.y);
        fputc(',', file);
        cell_float9(file, row->cpu_light.z);
    } else {
        fputs(",,", file);
    }
    fprintf(
        file, ",%lld,%lld,%llu\n", row->sky_tex_image, row->sky_tex_sub_image, row->sky_texture_id);
    return ferror(file) == 0;
}

static bool wrap_hours(double hours, double *wrapped)
{
    if (!isfinite(hours))
        return false;
    double value = fmod(hours, 24.0);
    if (value < 0)
        value += 24.0;
    if (value == 0.0)
        value = 0.0; // Fold -0.
    *wrapped = value;
    return true;
}

bool wake_lighting_review_begin(
    WakeLightingReview *review, const char *csv_path, WaterfrontGpuScene *scene)
{
    if (review == NULL)
        return false;
    if (review->state != NULL || active_review != NULL)
        return false; // Call end() before starting another review.
    if (csv_path == NULL || csv_path[0] == '\0')
        return false;

    ReviewState *state = calloc(1, sizeof(*state));
    if (state == NULL)
        return false;
    state->rows = calloc(REVIEW_MAX_ROWS, sizeof(*state->rows));
    if (state->rows == NULL) {
        free(state);
        return false;
    }
    state->capacity = REVIEW_MAX_ROWS;
    state->file = fopen(csv_path, "wb");
    if (state->file == NULL) {
        free(state->rows);
        free(state);
        return false;
    }

    if (scene != NULL) {
        if (scene->sky_mask.id == 0 || scene->sky_shader.id == 0
            || scene->sky_shader.id == rlGetShaderIdDefault())
            goto fail;
        int location = GetShaderLocation(scene->sky_shader, "sunDirection");
        if (location < 0)
            goto fail;
        state->scene = scene;
        state->sky_program = scene->sky_shader.id;
        state->sun_location = location;
        if (!spy_positive_control(state))
            goto fail;
        if (!install_spies(state, scene->sky_mask.id))
            goto fail;
    }

    review->state = state;
    return true;

fail:
    if (state->spies_installed)
        restore_spies(state);
    fclose(state->file);
    free(state->rows);
    free(state);
    return false;
}

void wake_lighting_review_frame(WakeLightingReview *review, const BoatCaptureSample *sample,
    uint64_t tick, double hours, double period, float rate, bool cycle_enabled,
    const Renderer3D *cpu)
{
    if (review == NULL || review->state == NULL || sample == NULL)
        return;
    ReviewState *state = review->state;
    if (state->count >= state->capacity) {
        if (!state->overflow_warned) {
            fprintf(stderr, "wake_lighting_review: %d-row capacity reached; recording stopped\n",
                REVIEW_MAX_ROWS);
            state->overflow_warned = true;
        }
        return;
    }

    if (!state->first_time_set) {
        state->first_time_set = true;
        state->first_time = sample->time;
    }

    ReviewRow *row = &state->rows[state->count++];
    memset(row, 0, sizeof(*row));
    row->time = sample->time - state->first_time;
    row->serial = sample->serial;
    row->tick = (unsigned long long)tick;
    row->clock_hours = hours;
    double canonical_hour = 0;
    row->canonical_hour = wrap_hours(hours, &canonical_hour) ? canonical_hour : NAN;
    row->period = period;
    row->rate = rate;
    row->cycle_enabled = cycle_enabled;
    row->active = sample->active;
    row->focused = sample->focused;
    row->paused = sample->paused;
    row->frame_ms = sample->frame_ms;
    row->interval_ms = sample->interval_ms;
    row->render_ms = sample->render_ms;
    row->has_glitter = waterfront_glitter_clock(hours, period, &row->glitter_seconds);

    row->sky_tex_image = -1;
    row->sky_tex_sub_image = -1;
    if (state->scene != NULL) {
        if (state->spies_installed) {
            row->sky_tex_image = state->sky_tex_image_calls;
            row->sky_tex_sub_image = state->sky_tex_sub_image_calls;
        }
        row->sky_texture_id = state->scene->sky_mask.id;
    }

    if (state->sun_pending) {
        row->sun_seen = true;
        row->sun = state->observed_sun;
    }
    state->sun_pending = false;

    if (cpu != NULL) {
        row->has_cpu = true;
        row->cpu_light = cpu->light_direction;
    }
}

void wake_lighting_review_gpu(WakeLightingReview *review, unsigned long long serial, double ms)
{
    if (review == NULL || review->state == NULL || !isfinite(ms))
        return;
    ReviewState *state = review->state;
    if (state->count == 0)
        return;

    size_t low = 0, high = state->count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (state->rows[middle].serial < serial)
            low = middle + 1;
        else
            high = middle;
    }
    if (low >= state->count || state->rows[low].serial != serial)
        return;
    float gpu_ms = (float)ms;
    if (!isfinite(gpu_ms))
        return;
    state->rows[low].gpu_ms = gpu_ms;
    state->rows[low].has_gpu = true;
}

bool wake_lighting_review_end(WakeLightingReview *review)
{
    if (review == NULL || review->state == NULL)
        return true;
    ReviewState *state = review->state;
    review->state = NULL;

    if (state->spies_installed)
        restore_spies(state);

    bool ok = state->file != NULL;
    if (state->file != NULL) {
        if (fputs(REVIEW_CSV_HEADER, state->file) == EOF)
            ok = false;
        for (size_t i = 0; i < state->count; ++i) {
            if (!write_row(state->file, &state->rows[i])) {
                ok = false;
                break;
            }
        }
        if (fclose(state->file) != 0)
            ok = false;
        state->file = NULL;
    }

    free(state->rows);
    free(state);
    return ok;
}

#else // __EMSCRIPTEN__

#include "wake_velocity/wake_velocity_lighting_review.h"

bool wake_lighting_review_begin(
    WakeLightingReview *review, const char *csv_path, WaterfrontGpuScene *scene)
{
    if (review != NULL)
        review->state = NULL;
    (void)csv_path;
    (void)scene;
    return false;
}

void wake_lighting_review_frame(WakeLightingReview *review, const BoatCaptureSample *sample,
    uint64_t tick, double hours, double period, float rate, bool cycle_enabled,
    const Renderer3D *cpu)
{
    (void)review;
    (void)sample;
    (void)tick;
    (void)hours;
    (void)period;
    (void)rate;
    (void)cycle_enabled;
    (void)cpu;
}

void wake_lighting_review_gpu(WakeLightingReview *review, unsigned long long serial, double ms)
{
    (void)review;
    (void)serial;
    (void)ms;
}

bool wake_lighting_review_end(WakeLightingReview *review)
{
    if (review != NULL)
        review->state = NULL;
    return true;
}

#endif
