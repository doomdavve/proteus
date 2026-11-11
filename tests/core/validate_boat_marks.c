// Plugin P5a: mark validation, digest and the per-slot entry detector in proteus_boat_core.
#include "boat_marks.h"
#include "boat_test_cases.h"

#include <stdio.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static BoatMarkSet two_marks(void)
{
    return (BoatMarkSet) { .count = 2,
        .items = { { .id = "west", .name = "WEST", .x = 100, .z = 0, .radius = 20 },
            { .id = "east", .name = "EAST", .x = -100, .z = 0, .radius = 20 } } };
}

static int validation(void)
{
    BoatMarkDiagnostic d;
    BoatMarkSet marks = two_marks();
    CHECK(boat_marks_validate(&marks, &d));
    BoatMarkSet bad = marks;
    bad.count = 0;
    CHECK(!boat_marks_validate(&bad, &d) && !strcmp(d.field, "marks"));
    bad = marks;
    bad.count = BOAT_MARKS_MAX + 1;
    CHECK(!boat_marks_validate(&bad, &d) && !strcmp(d.field, "marks"));
    bad = marks;
    strcpy(bad.items[1].id, "West");
    CHECK(
        !boat_marks_validate(&bad, &d) && !strcmp(d.field, "marks[2]") && strstr(d.message, "id"));
    bad = marks;
    strcpy(bad.items[1].name, " EAST");
    CHECK(!boat_marks_validate(&bad, &d) && strstr(d.message, "name"));
    bad = marks;
    bad.items[0].radius = 9.5f;
    CHECK(!boat_marks_validate(&bad, &d) && !strcmp(d.field, "marks[1]"));
    bad = marks;
    bad.items[0].radius = 201;
    CHECK(!boat_marks_validate(&bad, &d) && strstr(d.message, "radius"));
    bad = marks;
    bad.items[1].z = 0.0f / 0.0f;
    CHECK(!boat_marks_validate(&bad, &d) && strstr(d.message, "finite"));
    bad = marks;
    bad.items[1].x = 139; // Circles 39 m apart with 40 m of radii.
    CHECK(!boat_marks_validate(&bad, &d) && strstr(d.message, "overlaps"));
    bad.items[1].x = 140; // Touching is allowed.
    CHECK(boat_marks_validate(&bad, &d));
    bad = marks;
    strcpy(bad.items[1].id, "west");
    CHECK(!boat_marks_validate(&bad, &d) && strstr(d.message, "repeats"));
    // Two-digit field indices.
    BoatMarkSet many = { .count = BOAT_MARKS_MAX };
    for (unsigned i = 0; i < BOAT_MARKS_MAX; ++i) {
        snprintf(many.items[i].id, sizeof(many.items[i].id), "m%u", i);
        snprintf(many.items[i].name, sizeof(many.items[i].name), "M%u", i);
        many.items[i].x = (float)i * 100;
        many.items[i].radius = 10;
    }
    CHECK(boat_marks_validate(&many, &d));
    many.items[11].radius = 1;
    CHECK(!boat_marks_validate(&many, &d) && !strcmp(d.field, "marks[12]"));

    // The basin check keeps circles inside the walls.
    Vector2 walls = { 150, 150 };
    CHECK(boat_marks_check_map(&marks, NULL, walls, &d));
    walls.x = 110;
    CHECK(!boat_marks_check_map(&marks, NULL, walls, &d) && !strcmp(d.field, "marks[1]"));

    // The digest covers every field, names included.
    uint64_t digest = boat_marks_digest(&marks);
    CHECK(digest == boat_marks_digest(&marks));
    BoatMarkSet other = marks;
    strcpy(other.items[0].name, "WEST2");
    CHECK(boat_marks_digest(&other) != digest);
    other = marks;
    other.items[1].radius = 21;
    CHECK(boat_marks_digest(&other) != digest);
    other = marks;
    other.count = 1;
    CHECK(boat_marks_digest(&other) != digest);
    return 0;
}

static void place(BoatFleetState *fleet, unsigned slot, float x, float z)
{
    fleet->slots[slot].boat.position = (Vector2) { x, z };
}

static int detector(void)
{
    BoatMarkSet marks = two_marks();
    BoatFleetState fleet;
    memset(&fleet, 0, sizeof(fleet));
    BoatMarkTracker tracker;
    boat_marks_tracker_reset(&tracker);
    BoatMarkEntry out[BOAT_FLEET_SLOTS];
    // Inactive slots never report, even on a mark.
    place(&fleet, 0, 100, 0);
    CHECK(boat_marks_observe(&marks, &tracker, &fleet, out) == 0);
    // Slot 0 spawns outside, slot 2 spawns inside the east mark: only the entry is reported.
    for (unsigned s = 0; s < 3; s += 2) {
        fleet.slots[s].active = true;
        fleet.slots[s].life = 1;
    }
    place(&fleet, 0, 0, 0);
    place(&fleet, 2, -100, 5);
    CHECK(boat_marks_observe(&marks, &tracker, &fleet, out) == 1);
    CHECK(out[0].slot == 2 && out[0].mark == 1);
    // Staying inside reports nothing; slot 0 crosses the west edge (radius 20 is exclusive).
    place(&fleet, 0, 80, 0);
    CHECK(boat_marks_observe(&marks, &tracker, &fleet, out) == 0);
    place(&fleet, 0, 81, 0);
    CHECK(boat_marks_observe(&marks, &tracker, &fleet, out) == 1);
    CHECK(out[0].slot == 0 && out[0].mark == 0);
    place(&fleet, 0, 100, 19);
    CHECK(boat_marks_observe(&marks, &tracker, &fleet, out) == 0);
    // Leaving and coming back is a new entry.
    place(&fleet, 0, 100, 30);
    CHECK(boat_marks_observe(&marks, &tracker, &fleet, out) == 0);
    place(&fleet, 0, 100, 10);
    CHECK(boat_marks_observe(&marks, &tracker, &fleet, out) == 1 && out[0].slot == 0);
    // A new life re-arms the slot in place; deactivation forgets it.
    fleet.slots[2].life = 2;
    CHECK(boat_marks_observe(&marks, &tracker, &fleet, out) == 1);
    CHECK(out[0].slot == 2 && out[0].mark == 1);
    fleet.slots[2].active = false;
    CHECK(boat_marks_observe(&marks, &tracker, &fleet, out) == 0);
    fleet.slots[2].active = true;
    CHECK(boat_marks_observe(&marks, &tracker, &fleet, out) == 1 && out[0].slot == 2);
    // Several slots entering on one tick report in slot order.
    for (unsigned s = 0; s < BOAT_FLEET_SLOTS; ++s) {
        fleet.slots[s].active = true;
        fleet.slots[s].life = 9;
        place(&fleet, s, s % 2 ? -100.0f : 100.0f, (float)s);
    }
    CHECK(boat_marks_observe(&marks, &tracker, &fleet, out) == BOAT_FLEET_SLOTS);
    for (unsigned s = 0; s < BOAT_FLEET_SLOTS; ++s)
        CHECK(out[s].slot == s && out[s].mark == s % 2);
    // The tracker is plain data: a copy resumes identically.
    BoatMarkTracker copy = tracker;
    place(&fleet, 1, 0, 0);
    place(&fleet, 1, 100, 0);
    CHECK(boat_marks_observe(&marks, &tracker, &fleet, out) == 1 && out[0].mark == 0);
    CHECK(boat_marks_observe(&marks, &copy, &fleet, out) == 1 && out[0].mark == 0);
    CHECK(!memcmp(&copy, &tracker, sizeof(copy)));
    return 0;
}

static const TestCase cases[] = {
    TEST_CASE(validation),
    TEST_CASE(detector),
};

int main(int argc, char **argv)
{
    return test_cases_main(cases, TEST_CASE_COUNT(cases), argc, argv);
}
