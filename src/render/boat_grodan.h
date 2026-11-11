#ifndef PROTEUS_BOAT_GRODAN_H
#define PROTEUS_BOAT_GRODAN_H
#include "core/boat_model.h"
// GH3: the Grodan canal tour boat's visual recipe. HULL selects the hull (with its painted name);
// otherwise the open deck: benches, passengers, windscreen and helm. Bow toward -Z, waterline Y=0.
bool boat_grodan_generate(BoatModel *model, bool hull);
// Metres the Grodan's ensign staff stands aft of the speedboat's (boat_flag_attachment).
#define BOAT_GRODAN_FLAG_AFT 6.0f
BoatModelSpec boat_grodan_spec(void);
#endif
