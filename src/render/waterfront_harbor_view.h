#ifndef PROTEUS_WATERFRONT_HARBOR_VIEW_H
#define PROTEUS_WATERFRONT_HARBOR_VIEW_H

#include "core/waterfront_harbor.h"

// One presentation owner's generated hulls. Default recipes borrow immutable native meshes;
// customized boats own independent heap storage. Zero-initialize, never copy except through
// prepare/commit/discard below, and destroy with the owning CPU/GPU view.
typedef struct {
    BoatModel *boats[BOAT_SCENE_BERTHS];
    BoatModelSpec specs[BOAT_SCENE_BERTHS];
    unsigned char recipes[BOAT_SCENE_BERTHS];
    unsigned count, builds;
    bool mirror_lettering; // Fixed per owner before first prepare; CPU camera compensation.
} WaterfrontHarborModels;

// Prepare a complete replacement without changing ACTIVE. On failure no allocation survives.
// A GPU owner may upload changed candidate meshes before committing; discard on upload failure.
bool waterfront_harbor_models_prepare(WaterfrontHarborModels *candidate,
    const WaterfrontHarborModels *active, const WaterfrontHarborFrame *frame);
void waterfront_harbor_models_commit(
    WaterfrontHarborModels *active, WaterfrontHarborModels *candidate);
void waterfront_harbor_models_discard(
    WaterfrontHarborModels *candidate, const WaterfrontHarborModels *active);
void waterfront_harbor_models_destroy(WaterfrontHarborModels *models);
const Mesh3D *waterfront_harbor_models_mesh(const WaterfrontHarborModels *models, unsigned index);
size_t waterfront_harbor_models_bytes(const WaterfrontHarborModels *models);

#endif
