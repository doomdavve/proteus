#include "core/boat_archipelago.h"
#include "core/boat_scene_definition.h"

#include <math.h>

#include "../../assets/geometry/archipelago/life.h"
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

BoatModelSpec boat_archipelago_fishing_spec(void)
{
    BoatModelSpec s = boat_model_preset(BOAT_MODEL_WORKBOAT);
    s.bow_width = .12f;
    s.stern_width = .7f;
    s.cabin_length = 2.8f;
    s.cabin_offset = 2.1f;
    s.windshield_rake = .04f;
    s.mast_height = 6.4f;
    s.work_deck = false;
    s.fenders = true;
    s.hull = (Color) { 203, 151, 53, 255 };
    s.deck = (Color) { 156, 116, 65, 255 };
    s.trim = (Color) { 70, 62, 45, 255 };
    return s;
}

const BoatSceneDefinition *boat_archipelago_life(void) { return &archipelago_life; }

const BoatArchipelagoResident *boat_archipelago_residents(size_t *count)
{
    if (count)
        *count = sizeof(archipelago_residents) / sizeof(archipelago_residents[0]);
    return archipelago_residents;
}

float boat_archipelago_ground(Vector2 position)
{
    float u = (position.x + 1600) / 32, v = (position.y + 3104) / 32;
    if (!(u >= 0 && v >= 0 && u < archipelago_ground_columns - 1
            && v < archipelago_ground_rows - 1))
        return 0;
    int x = (int)floorf(u), z = (int)floorf(v);
    float tx = u - x, tz = v - z;
    const unsigned short *a = archipelago_ground_cm + z * archipelago_ground_columns + x;
    const unsigned short *b = a + archipelago_ground_columns;
    return ((a[0] + (a[1] - (float)a[0]) * tx) * (1 - tz) + (b[0] + (b[1] - (float)b[0]) * tx) * tz)
        * .01f;
}
