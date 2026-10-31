#ifndef PROTEUS_BOAT_FIXTURES_H
#define PROTEUS_BOAT_FIXTURES_H

// Fixture data for validators and review tools. The engine no longer selects content from
// compiled definitions; this library keeps the shipped values so tests can compare packages
// against the reference they replaced. No production target links it.
#include "boat_combat.h"
#include "boat_course.h"
#include "boat_match.h"
#include "boat_race.h"
#include "boat_voyage.h"

extern const BoatVoyageRoute boat_fixture_voyage_outer;
extern const BoatCourseGate boat_fixture_course_gates[BOAT_COURSE_GATES];
extern const BoatCourseDefinition boat_fixture_course_default;
extern const BoatRaceCourse boat_fixture_river_course;
extern const BoatRaceRules boat_fixture_race_default_rules;
extern const BoatMatchRules boat_fixture_match_default_rules;
extern const BoatCombatRules boat_fixture_combat_default_rules;

#endif
