#include "room/boat_room_record_objective.h"

#include <stdio.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static uint8_t bytes[BOAT_ROOM_RECORD_OBJECTIVE_BYTES_MAX];
static uint8_t other[BOAT_ROOM_RECORD_OBJECTIVE_BYTES_MAX];
static BoatRoomObjective original, restored;
static BoatRoomObjectiveSource source, candidate;

int main(void)
{
    static const char code[]
        = "return { init=function(c) return {n=0}, {} end, "
          "update=function(c,s) s.n=s.n+#c.events; return s, "
          "{{kind='hud.set',generation=c.generation,id='n',label='N',value=s.n}} end, "
          "migrate=function(c,s) return s, {} end }";
    source.present = true;
    source.state_version = 1;
    source.migrates = true;
    source.source_size = sizeof(code) - 1;
    memcpy(source.source, code, source.source_size);
    CHECK(boat_room_objective_init(&original, &source));
    BoatNetRuleEvent join = { .kind = BOAT_NET_RULE_CREW_JOINED, .slot = 1 };
    boat_room_objective_event(&original, 3, &join);
    candidate = source;
    CHECK(boat_room_objective_stage(&original, &candidate, NULL));
    size_t size = boat_room_record_objective_encode(&original, bytes, sizeof(bytes));
    CHECK(size && size <= sizeof(bytes));
    CHECK(boat_room_record_objective_decode(bytes, size, &source, &restored));
    CHECK(restored.source == &restored.owned);
    CHECK(restored.has_pending && restored.present[1] && restored.event_count == 1);
    CHECK(boat_room_record_objective_encode(&restored, other, sizeof(other)) == size);
    CHECK(!memcmp(bytes, other, size));

    // A seek before the reload boundary preserves queued joins and migration, not just HUD.
    CHECK(boat_room_objective_after_tick(&original, 12));
    CHECK(boat_room_objective_after_tick(&restored, 12));
    size = boat_room_record_objective_encode(&original, bytes, sizeof(bytes));
    CHECK(size && boat_room_record_objective_encode(&restored, other, sizeof(other)) == size);
    CHECK(!memcmp(bytes, other, size));
    CHECK(restored.objective.command_count == 1 && restored.objective.hud[0].value == 1);

    // Suspension and the accepted commands survive independently of the objective digest.
    original.objective.quarantined = true;
    original.diagnostic.status = BOAT_PLUGIN_RUNTIME;
    snprintf(original.diagnostic.message, sizeof(original.diagnostic.message), "fixture failed");
    original.has_revision = true;
    original.revision_source = source;
    size = boat_room_record_objective_encode(&original, bytes, sizeof(bytes));
    CHECK(size && boat_room_record_objective_decode(bytes, size, &source, &restored));
    CHECK(restored.objective.quarantined && restored.has_revision);
    CHECK(restored.objective.command_count == 1);
    CHECK(!strcmp(restored.diagnostic.message, "fixture failed"));

    // Every incomplete prefix and trailing data fail atomically, as do unknown versions.
    for (size_t i = 0; i < size; ++i) {
        restored.sequence = 777;
        CHECK(!boat_room_record_objective_decode(bytes, i, &source, &restored));
        CHECK(restored.sequence == 777);
    }
    bytes[size] = 0;
    CHECK(!boat_room_record_objective_decode(bytes, size + 1, &source, &restored));
    bytes[0] = 2;
    CHECK(!boat_room_record_objective_decode(bytes, size, &source, &restored));
    bytes[0] = 1;
    CHECK(!boat_room_record_objective_encode(&original, other, size - 1));
    original.objective.command_count = BOAT_PLUGIN_COMMANDS + 1;
    CHECK(!boat_room_record_objective_encode(&original, other, sizeof(other)));
    original.objective.command_count = 1;
    original.event_count = BOAT_PLUGIN_EVENTS + 1;
    CHECK(!boat_room_record_objective_encode(&original, other, sizeof(other)));
    original.event_count = 0;

    // Both maximum-size staged sources fit the advertised capacity without raw struct bytes.
    original.has_pending = true;
    original.pending = source;
    original.pending.source_size = BOAT_PLUGIN_SOURCE_MAX;
    memset(original.pending.source, ' ', sizeof(original.pending.source));
    original.revision_source = original.pending;
    size = boat_room_record_objective_encode(&original, bytes, sizeof(bytes));
    CHECK(size && boat_room_record_objective_decode(bytes, size, &source, &restored));
    CHECK(restored.pending.source_size == BOAT_PLUGIN_SOURCE_MAX);
    CHECK(restored.revision_source.source_size == BOAT_PLUGIN_SOURCE_MAX);
    puts("boat_room_record_objective=ok");
    return 0;
}
