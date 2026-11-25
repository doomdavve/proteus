#include "core/boat_archipelago.h"

#include "../../assets/geometry/archipelago/places.h"
#include "../../assets/geometry/archipelago/water.h"

static const Mesh3D archipelago = { archipelago_water_vertices, archipelago_water_triangles,
    sizeof(archipelago_water_triangles) / sizeof(archipelago_water_triangles[0]) };

const BoatMapDescription boat_archipelago_map = { &archipelago,
    sizeof(archipelago_water_triangles) / sizeof(archipelago_water_triangles[0]) };

const BoatArchipelagoPlace *boat_archipelago_places(size_t *count)
{
    if (count)
        *count = sizeof(archipelago_places) / sizeof(archipelago_places[0]);
    return archipelago_places;
}
