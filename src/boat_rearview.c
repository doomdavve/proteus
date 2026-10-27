#include "boat_rearview.h"

bool boat_rearview_update(BoatRearview *m, BoatGpu *gpu, const BoatViewState *view,
    const BoatPresentation *presentation, const BoatWorld *world, const BoatRivals *rivals)
{
    if (m->sampled && m->location == world->definition && view->time >= m->sampled_time
        && view->time - m->sampled_time + 1e-9 < 1.0 / 30)
        return true;
    BoatCamera camera = { .rear_view = true };
    m->gpu = gpu != NULL;
    if (gpu) {
        if (!m->target.id) {
            m->target = LoadRenderTexture(BOAT_REARVIEW_WIDTH, BOAT_REARVIEW_HEIGHT);
            if (!IsRenderTextureValid(m->target))
                return false;
            SetTextureFilter(m->target.texture, TEXTURE_FILTER_POINT);
        }
        RenderTexture2D main_target = gpu->target, main_supersampled = gpu->supersampled;
        WaterfrontGpuStats main_stats = gpu->scene.stats;
        gpu->target = m->target;
        gpu->supersampled = (RenderTexture2D) { 0 };
        boat_gpu_draw_scene(gpu, view, presentation, world, camera, rivals, NULL, NULL, 0);
        gpu->target = main_target;
        gpu->supersampled = main_supersampled;
        gpu->scene.stats = main_stats;
        if (gpu->failed)
            return false;
    } else {
        if (!boat_draw_scene(&m->renderer, &m->cpu, m->pixels, BOAT_REARVIEW_WIDTH,
                BOAT_REARVIEW_HEIGHT, view, presentation, world, camera, rivals, NULL, NULL, 0))
            return false;
        if (!m->texture.id) {
            m->texture = LoadTextureFromImage((Image) { m->pixels, BOAT_REARVIEW_WIDTH,
                BOAT_REARVIEW_HEIGHT, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 });
            if (!IsTextureValid(m->texture))
                return false;
            SetTextureFilter(m->texture, TEXTURE_FILTER_POINT);
        } else
            UpdateTexture(m->texture, m->pixels);
    }
    m->sampled = true;
    m->sampled_time = view->time;
    m->location = world->definition;
    return true;
}

void boat_rearview_draw(const BoatRearview *m, int width)
{
    if (!m->sampled)
        return;
    int x = (width - BOAT_REARVIEW_WIDTH) / 2, y = BOAT_REARVIEW_TOP;
    DrawRectangle(x - 3, y - 3, BOAT_REARVIEW_WIDTH + 6, BOAT_REARVIEW_HEIGHT + 6,
        (Color) { 8, 20, 25, 255 });
    DrawRectangle(x - 2, y - 2, BOAT_REARVIEW_WIDTH + 4, BOAT_REARVIEW_HEIGHT + 4,
        (Color) { 133, 158, 153, 255 });
    Texture2D texture = m->gpu ? m->target.texture : m->texture;
    // A real mirror reverses camera left/right. Native GPU X is already opposite CPU X;
    // flip only GPU Y, and only CPU X, to keep a starboard pursuer on the mirror's right.
    DrawTexturePro(texture,
        (Rectangle) { 0, 0, m->gpu ? BOAT_REARVIEW_WIDTH : -BOAT_REARVIEW_WIDTH,
            m->gpu ? -BOAT_REARVIEW_HEIGHT : BOAT_REARVIEW_HEIGHT },
        (Rectangle) { x, y, BOAT_REARVIEW_WIDTH, BOAT_REARVIEW_HEIGHT }, (Vector2) { 0 }, 0, WHITE);
}

void boat_rearview_destroy(BoatRearview *m)
{
    if (m->target.id)
        UnloadRenderTexture(m->target);
    if (m->texture.id)
        UnloadTexture(m->texture);
    boat_cpu_view_destroy(&m->cpu);
    renderer3d_destroy(&m->renderer);
    *m = (BoatRearview) { 0 };
}
