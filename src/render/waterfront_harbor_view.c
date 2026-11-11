#include "render/waterfront_harbor_view.h"

#include <stdlib.h>
#include <string.h>

void waterfront_harbor_models_discard(
    WaterfrontHarborModels *candidate, const WaterfrontHarborModels *active)
{
    for (unsigned i = 0; i < BOAT_SCENE_BERTHS; ++i)
        if (candidate->boats[i] != active->boats[i])
            free(candidate->boats[i]);
    memset(candidate, 0, sizeof(*candidate));
}

bool waterfront_harbor_models_prepare(WaterfrontHarborModels *candidate,
    const WaterfrontHarborModels *active, const WaterfrontHarborFrame *frame)
{
    memset(candidate, 0, sizeof(*candidate));
    if (frame->berth_count < 0 || frame->berth_count > BOAT_SCENE_BERTHS)
        return false;
    candidate->count = (unsigned)frame->berth_count;
    candidate->mirror_lettering = active->mirror_lettering;
    candidate->builds = active->builds;
    for (unsigned i = 0; i < candidate->count; ++i) {
        BoatModelSpec spec = frame->specs[i];
        if (frame->recipes[i] >= BOAT_SCENE_BERTH_RECIPES || !boat_model_spec_valid(spec))
            goto failed;
        candidate->recipes[i] = frame->recipes[i];
        candidate->specs[i] = spec;
        if (i < active->count && boat_model_spec_equal(&spec, &active->specs[i])) {
            candidate->boats[i] = active->boats[i];
            continue;
        }
        BoatModelSpec base = boat_scene_berth_spec((BoatSceneBerthRecipe)frame->recipes[i]);
        if (boat_model_spec_equal(&spec, &base))
            continue;
        candidate->boats[i] = malloc(sizeof(BoatModel));
        if (!candidate->boats[i]
            || !boat_model_generate_oriented(candidate->boats[i], spec, active->mirror_lettering))
            goto failed;
        ++candidate->builds;
    }
    return true;
failed:
    waterfront_harbor_models_discard(candidate, active);
    return false;
}

void waterfront_harbor_models_commit(
    WaterfrontHarborModels *active, WaterfrontHarborModels *candidate)
{
    for (unsigned i = 0; i < BOAT_SCENE_BERTHS; ++i)
        if (active->boats[i] != candidate->boats[i])
            free(active->boats[i]);
    *active = *candidate; // Only owned pointers and copied specs, never BoatModel's mesh view.
    memset(candidate, 0, sizeof(*candidate));
}

void waterfront_harbor_models_destroy(WaterfrontHarborModels *models)
{
    for (unsigned i = 0; i < BOAT_SCENE_BERTHS; ++i)
        free(models->boats[i]);
    memset(models, 0, sizeof(*models));
}

const Mesh3D *waterfront_harbor_models_mesh(const WaterfrontHarborModels *models, unsigned index)
{
    if (index >= models->count)
        return NULL;
    return models->boats[index] ? &models->boats[index]->mesh
                                : waterfront_harbor_recipe_mesh(models->recipes[index]);
}

size_t waterfront_harbor_models_bytes(const WaterfrontHarborModels *models)
{
    size_t bytes = sizeof(*models);
    for (unsigned i = 0; i < models->count; ++i)
        if (models->boats[i])
            bytes += sizeof(BoatModel);
    return bytes;
}
