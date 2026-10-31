#include "boat_fixtures.h"

// The outer location's first-slice outing, copied from src/boat_voyage.c before compiled
// selection was removed.
const BoatVoyageRoute boat_fixture_voyage_outer = {
    .shelters = { { "DOCK", { -12, 60 }, 0, 60 }, { "BRIDGE", { 650, -850 }, 1.33f, 150 },
        { "EAST SHORE", { 3250, -2000 }, .86f, 200 },
        { "LANGEDRAG", { 3600, -3370 }, 1.57f, 0 } },
    .shelter_count = 4,
    .arrival_centre = { 3595, -3370 },
    .arrival_radius = 75,
    .arrival_speed = 6,
    .exposure = { .centre = { 4300, -2650 }, .radius = 700, .height = 1.6f, .build = 60 },
    .exposed_fraction = .8f,
    .weather_range = 2500,
};

// Northbound gates; offset centres encourage two broad turns across the basin.
const BoatCourseGate boat_fixture_course_gates[BOAT_COURSE_GATES]
    = { { { 0, 35 }, 12 }, { { 22, 7 }, 10 }, { { -22, -23 }, 10 }, { { 0, -53 }, 12 } };

const BoatCourseDefinition boat_fixture_course_default = {
    .start = { 0, 50 },
    .heading = 0,
    .gates = { { { 0, 35 }, 12 }, { { 22, 7 }, 10 }, { { -22, -23 }, 10 }, { { 0, -53 }, 12 } },
};

// The shortened river sprint, copied from src/boat_race.c.
const BoatRaceCourse boat_fixture_river_course = {
    .start = { { -180, -250 }, { .786f, -.618f }, 40 },
    .gates = { { { -72.3f, -376.6f }, { .830f, -.558f }, 18 },
        { { 99.7f, -371.8f }, { .830f, -.558f }, 18 },
        { { 146.2f, -535.5f }, { .830f, -.558f }, 16 },
        { { 315.3f, -516.9f }, { .830f, -.558f }, 16 },
        { { 375.9f, -678.0f }, { .830f, -.558f }, 18 },
        { { 520.1f, -678.5f }, { .830f, -.558f }, 20 },
        { { 588.5f, -767.2f }, { .767f, -.641f }, 158 } },
    .gate_count = 7,
    .storm = { .centre = { 46.2f, -399.12f },
        .velocity = { 6.64f, -4.464f },
        .radius = 140,
        .height = 1.6f },
    .grid
    = { { -202.4f, -259.1f }, { -193.8f, -248.1f }, { -185.1f, -237.1f }, { -176.5f, -226.1f } },
    .source = { .origin = { -118.966f, -498.230f },
        .direction = { .617822f, .786318f },
        .started = -12,
        .speed = 12,
        .height = 3,
        .duration = 24,
        .lifetime = 40,
        .wavelength = 8,
        .spread = 3,
        .half_width = 14 },
};

const BoatRaceRules boat_fixture_race_default_rules = { .grid_ticks = 1 * BOAT_TICK_HZ,
    .countdown_ticks = 3 * BOAT_TICK_HZ,
    .results_ticks = 8 * BOAT_TICK_HZ,
    .after_winner_ticks = 15 * BOAT_TICK_HZ,
    .cap_ticks = 90 * BOAT_TICK_HZ,
    .recovery_ticks = 2 * BOAT_TICK_HZ,
    .skip_ticks = BOAT_TICK_HZ / 2,
    .park_ticks = 2 * BOAT_TICK_HZ,
    .min_players = 2 };

const BoatMatchRules boat_fixture_match_default_rules = { .round_ticks = 300 * BOAT_TICK_HZ,
    .intermission_ticks = 10 * BOAT_TICK_HZ,
    .credit_ticks = 5 * BOAT_TICK_HZ,
    .grace_ticks = 2 * BOAT_TICK_HZ,
    .respawn_ticks = 3 * BOAT_TICK_HZ,
    .skip_ticks = 1 * BOAT_TICK_HZ,
    .contact_impulse = 1.5f,
    .wake_height = .35f,
    .min_players = 2 };

const BoatCombatRules boat_fixture_combat_default_rules = { .health = 100,
    .damage = 0,
    .bullet_speed = 94,
    .bullet_range = 70,
    .grenade_distance = 7,
    .wave_height = 4,
    .wave_speed = 10,
    .wave_range = 70,
    .cannon_pressure = .1f,
    .pressure_recovery = .22f,
    .torpedo_damage = 40,
    .torpedo_speed = 32,
    .torpedo_range = 160,
    .torpedo_ticks = 180,
    .arming_ticks = 30,
    .torpedo_ammo = 3,
    .fire_ticks = 15,
    .heat_ticks = 480,
    .cool_ticks = 360,
    .grenade_ticks = 960,
    .splash_ticks = 36,
    .fuse_ticks = 120,
    .grace_ticks = 240 };
