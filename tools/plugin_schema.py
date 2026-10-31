"""Machine-readable API 1 schemas. Native semantic validation remains authoritative.

JSON Schema describes a data projection of Lua records, not Lua functions or source syntax.
Cross-field geometry, ownership and callback/state rules are explicit native constraints.
"""
import math


def record(properties):
    return {"type": "object", "additionalProperties": False,
            "required": list(properties), "properties": properties}


def object_schema(properties, required=None):
    """A strict object with an explicit required subset (all keys by default)."""
    required = list(properties) if required is None else list(required)
    return {"type": "object", "additionalProperties": False,
            "required": required, "properties": properties}


def number(low, high, unit=None, integer=False):
    result = {"type": "integer" if integer else "number", "minimum": low, "maximum": high}
    if unit:
        result["description"] = unit
    return result


def text(limit=23, pattern=r"^[ -~]+$"):
    return {"type": "string", "minLength": 1, "maxLength": limit, "pattern": pattern}


def sequence(item, low, high):
    return {"type": "array", "items": item, "minItems": low, "maxItems": high}


def schema():
    point = {"x": number(-20000, 20000, "metres; x = -east"),
             "z": number(-20000, 20000, "metres; z = north")}
    gate = record({**point, "dx": number(-1, 1), "dz": number(-1, 1),
                   "half_width": number(4, 500, "metres")})
    shelter = record({"id": text(pattern=r"^[a-z][a-z0-9_]*$"),
                      "name": text(pattern=r"^[A-Z0-9.\-' ]+$"), **point,
                      "heading": number(-math.tau, math.tau, "radians"),
                      "radius": number(0, 1000, "metres; >0 except last, which must be 0")})
    voyage = record({"shelters": sequence(shelter, 2, 4),
                     "arrival": record({**point, "radius": number(1, 1000, "metres"),
                                        "max_speed": number(.5, 50, "m/s")}),
                     "exposure": record({**point, "radius": number(0, 5000, "metres"),
                                         "height": number(0, 5, "metres"),
                                         "build": number(0, 600, "seconds")}),
                     "exposed_fraction": number(.05, 1),
                     "weather_range": number(0, 20000, "metres")})
    rule_lows = {"grid_ticks": 0, "countdown_ticks": 1, "results_ticks": 0,
                 "after_winner_ticks": 1, "cap_ticks": 1, "recovery_ticks": 0,
                 "skip_ticks": 0, "park_ticks": 0}
    race = record({"start": gate, "gates": sequence(gate, 1, 8),
                   "grid": sequence(record(point), 4, 4),
                   "source": record({**point, "dx": number(-1, 1), "dz": number(-1, 1),
                                     "started": number(-600, 600, "seconds relative to GO"),
                                     "speed": number(.1, 30, "m/s"),
                                     "height": number(.001, 5, "metres"),
                                     "duration": number(1, 60, "seconds"),
                                     "lifetime": number(1, 60, "seconds"),
                                     "wavelength": number(2, 40, "metres"),
                                     "spread": number(1, 10, "m/s"),
                                     "half_width": number(2, 80, "metres")}),
                   "storm": record({**point, "vx": number(-50, 50, "m/s"),
                                    "vz": number(-50, 50, "m/s"),
                                    "radius": number(0, 5000, "metres"),
                                    "height": number(0, 5, "metres"),
                                    "started": number(-600, 600, "seconds relative to GO"),
                                    "build": number(0, 600, "seconds")}),
                   "rules": record({**{k: number(v, 432000, "120 Hz ticks", True)
                                        for k, v in rule_lows.items()},
                                    "min_players": number(1, 4, integer=True)})})
    manifest = record({"id": text(63, r"^[a-z][a-z0-9._-]*$"), "name": text(63),
                       "api": {"const": 1},
                       "kind": {"enum": ["voyage", "free", "course", "race", "match"]},
                       "entry": {"const": "main.lua"},
                       "location": {"enum": ["basin", "river", "outer", "any"]},
                       "capabilities": {"enum": [["voyage.define"],
                           ["voyage.define", "scene.define"],
                           ["voyage.define", "objective.hud"],
                           ["voyage.define", "scene.define", "objective.hud"],
                           ["voyage.define", "objective.hud", "objective.timers"],
                           ["voyage.define", "scene.define", "objective.hud", "objective.timers"],
                           ["free.define"], ["free.define", "scene.define"], ["course.define"],
                            ["race.define"], ["race.define", "scene.define"],
                            ["match.define"], ["match.define", "scene.define"]]},
                       "state_version": number(1, 65535, integer=True)})
    manifest["allOf"] = [
        {"if": {"properties": {"kind": {"const": "race"}}},
         "then": {"properties": {"location": {"const": "river"},
             "capabilities": {"const": ["race.define"]}, "state_version": {"const": 1}}}},
        {"if": {"properties": {"kind": {"const": "course"}}},
         "then": {"properties": {"location": {"const": "basin"},
             "capabilities": {"const": ["course.define"]}, "state_version": {"const": 1}}}},
        {"if": {"properties": {"kind": {"const": "free"}}},
         "then": {"properties": {"location": {"enum": ["basin", "river"]},
             "capabilities": {"const": ["free.define"]}, "state_version": {"const": 1}}}},
        {"if": {"properties": {"kind": {"const": "voyage"}}},
         "then": {"properties": {"location": {"const": "outer"},
             "capabilities": {"not": {"const": ["race.define"]}}}}},
        {"if": {"properties": {"capabilities": {"const": ["voyage.define"]}}},
         "then": {"properties": {"state_version": {"const": 1}}}},
        {"if": {"properties": {"capabilities": {"const": ["voyage.define", "scene.define"]}}},
         "then": {"properties": {"state_version": {"const": 1}}}}]
    # L2.2 exposes marina berths; registered dock modules remain a planned projection so
    # discovery does not claim fields the decoder still rejects.
    berth = record({"id": text(23, r"^[a-z][a-z0-9_]*$"),
                    "recipe": {"enum": ["skiff", "runabout", "sailboat", "cruiser", "speedboat",
                                        "sailboat_7m", "workboat"]},
                    **point, "heading": number(-math.tau, math.tau, "radians"),
                    "mooring": {"enum": ["pontoon", "none"]}})
    module = record({"id": text(23, r"^[a-z][a-z0-9_]*$"),
                     "kind": {"enum": ["pontoon", "pontoon_small", "gangway", "quay"]},
                     **point, "heading": number(-math.tau, math.tau, "radians"),
                     "anchor": record(point), "attaches": text(23)})
    marina = object_schema({"entrance": record({**point, "width": number(4, 40, "metres")}),
                            "berths": sequence(berth, 0, 7),
                            "modules": sequence(module, 0, 4)}, required=())
    routes = ["river_runabout", "river_cruiser", "river_sailboat", "river_ferry", "alvsnabben",
              "frihamnen", "schooner", "marina_pass"]
    craft = ["runabout", "cruiser", "sailboat", "ferry", "alvsnabben", "frihamnen", "schooner",
             "workboat"]
    stop = record({"id": text(23, r"^[a-z][a-z0-9_]*$"),
                   "dwell": number(0, 86400, "seconds")})
    vessel = {"oneOf": [
        record({"id": text(23, r"^[a-z][a-z0-9_]*$"), "retain": {"enum": routes},
                "stops": sequence(stop, 0, 8)}),
        record({"id": text(23, r"^[a-z][a-z0-9_]*$"), "recipe": {"enum": craft},
                "route": {"enum": routes}, "speed": number(0, 40, "m/s"),
                "phase": number(-86400, 86400, "seconds"),
                "period": number(5, 86400, "seconds"),
                "stops": sequence(stop, 0, 8)})]}
    traffic = object_schema({"vessels": sequence(vessel, 0, 8)}, required=("vessels",))
    cue = object_schema({"kind": {"enum": ["feeding", "alarm"]},
                         "start": number(0, 0xffffffff, "120 Hz ticks", integer=True),
                         "stop": number(0, 0xffffffff, "120 Hz ticks", integer=True),
                         "fade": number(0, 0xffffffff, "120 Hz ticks", integer=True),
                         "amplitude": number(0, 1, "scale")},
        required=("kind", "start", "stop", "amplitude"))
    flock = record({"id": text(23, r"^[a-z][a-z0-9_]*$"), "recipe": {"enum": ["gull"]},
                    **point, "count": number(1, 8, "birds", integer=True),
                    "seed": number(0, 0xffffffff, "phase seed", integer=True),
                    "phase": number(-86400, 86400, "seconds"),
                    "radius": number(.5, 200, "metres"), "height": number(.5, 200, "metres"),
                    "rate": number(-5, 5, "turns/second"), "cues": sequence(cue, 0, 16)})
    gulls = object_schema({"flocks": sequence(flock, 0, 4)}, required=("flocks",))
    scene = object_schema({"version": {"const": 1}, "marina": marina, "traffic": traffic,
                           "gulls": gulls}, required=("version",))
    match_rules = record({"round_ticks": number(1, 432000, "120 Hz ticks", True),
                          "intermission_ticks": number(0, 432000, "120 Hz ticks", True),
                          "credit_ticks": number(1, 432000, "120 Hz ticks", True),
                          "grace_ticks": number(0, 432000, "120 Hz ticks", True),
                          "respawn_ticks": number(0, 432000, "120 Hz ticks", True),
                          "skip_ticks": number(0, 432000, "120 Hz ticks", True),
                          "contact_impulse": number(.0001, 100000, "impulse"),
                          "wake_height": number(.0001, 100, "metres"),
                          "min_players": number(1, 4, integer=True)})
    combat_fields = {"health": number(1, 100000), "damage": number(0, 100000),
                     "bullet_speed": number(1, 10000, "m/s"), "bullet_range": number(1, 10000, "m"),
                     "grenade_distance": number(0, 10000, "m"),
                     "wave_height": number(0, 100), "wave_speed": number(0, 10000, "m/s"),
                     "wave_range": number(1, 10000, "m"),
                     "cannon_pressure": number(0, 100), "pressure_recovery": number(0, 100),
                     "torpedo_damage": number(0, 100000), "torpedo_speed": number(1, 10000, "m/s"),
                     "torpedo_range": number(1, 10000, "m"),
                     "torpedo_ticks": number(0, 432000, "120 Hz ticks", True),
                     "arming_ticks": number(0, 432000, "120 Hz ticks", True),
                     "torpedo_ammo": number(1, 432000, integer=True),
                     "fire_ticks": number(0, 432000, "120 Hz ticks", True),
                     "heat_ticks": number(0, 432000, "120 Hz ticks", True),
                     "cool_ticks": number(0, 432000, "120 Hz ticks", True),
                     "grenade_ticks": number(0, 432000, "120 Hz ticks", True),
                     "splash_ticks": number(0, 432000, "120 Hz ticks", True),
                     "fuse_ticks": number(0, 432000, "120 Hz ticks", True),
                     "grace_ticks": number(0, 432000, "120 Hz ticks", True)}
    combat = record(combat_fields)
    free = record({"version": {"const": 1}})
    course_gate = record({**point, "half_width": number(4, 500, "metres")})
    course = record({"start": record({**point, "heading": number(-math.tau, math.tau, "radians")}),
                     "gates": sequence(course_gate, 4, 4)})
    int64 = number(-(1 << 63), (1 << 63) - 1, integer=True)
    command_base = {"generation": number(1, 0xffffffff, integer=True), "id": text()}
    command_variants = []
    for kind, extra in (("hud.set", {"label": text(), "value": int64}), ("hud.remove", {}),
                        ("timer.set", {"tick": int64}), ("timer.cancel", {})):
        command_variants.append(record({"kind": {"const": kind}, **command_base, **extra}))
    commands = sequence({"oneOf": command_variants}, 0, 32)
    bundle = record({"format": {"const": "wake-velocity-plugin"}, "version": {"const": 1},
                     "manifest": {"type": "string", "maxLength": 4096},
                     "source": {"type": "string", "maxLength": 65536}})
    bundle["description"] = "UTF-8 source bundle, at most 200000 bytes; manifest/source byte caps apply after encoding"
    # A race, free or match definition may carry the same authored scene envelope.
    race = object_schema({**race["properties"], "scene": scene}, required=race["required"])
    free = object_schema({**free["properties"], "scene": scene}, required=("version",))
    match = object_schema({"version": {"const": 1}, "rules": match_rules, "combat": combat,
                           "scene": scene}, required=("version", "rules", "combat"))
    definitions = {"bundle": bundle, "manifest": manifest, "voyage": voyage, "scene": scene,
                   "race": race, "free": free, "course": course, "match": match,
                   "commands": commands}
    for value in definitions.values():
        value["$schema"] = "https://json-schema.org/draft/2020-12/schema"
    return {"protocol": 1, "api": 1, "schemas": definitions,
            "representation": "JSON projection of Lua data records; functions are described below",
            "native_constraints": {
                "manifest": ["Strict JSON; duplicate keys forbidden; at most 4096 bytes"],
                "voyage": ["Unique shelter IDs; HUD names have no edge spaces",
                    "Last shelter radius 0; others >0; last shelter inside arrival",
                    "No shelter in exposed water; first shelter equals selected location spawn",
                    "Shelters have 8m water clearance; arrival 20m"],
                "scene": ["marina.berths is 0..7; missing berths keeps the compiled collection",
                    "marina.modules is 0..4; missing modules keeps the compiled dock furniture",
                    "Berth/module IDs are unique scene-wide; recipes are registered native hulls",
                    "Hull plus mooring excursion must clear compiled water, other berths, the",
                    "declared entrance and the spawn/recovery shelters; depth is unverified",
                    "Pontoon/quay footprints must clear each other; gangways need a land anchor,",
                    "water landing, 2..20 m span and an existing pontoon target",
                    "scene.traffic.vessels is 0..8; a present list replaces all travelling craft",
                    "Retain entries copy a compiled actor and may override its named stop dwells;",
                    "authored entries select a registered route and recipe. Stop IDs must match",
                    "the preset in order; omitted dwell shares the period remainder",
                    "Hulls must clear the map, marina and scheduled craft",
                    "scene.gulls.flocks is 0..4 with 1..8 birds each and at most 32 total",
                    "Cues use half-open 120 Hz tick intervals, 0..1 amplitude and optional fade",
                    "Alarm overrides feeding, which overrides circling; no timer callbacks",
                    "Ordinary traffic wakes stay visual; no authored route wake recipe exists",
                    "scene.define requires voyage.define and precedes objective grants"],
                "race": ["Gate directions have unit length within .01; source squared tolerance .0001",
                    "skip_ticks <= recovery_ticks; native course/rules validation",
                    "Grid, gate corridors and ferry path pass compiled-map clearance checks"],
                "free": ["free.version must be 1; content stays the engine's registered defaults"],
                "course": ["Exactly four gates south of the start and in strictly decreasing z",
                    "The gate centre and both edges must be open water; basin gates stay in bounds",
                    "The compiled course's digest keeps existing course-*.run bests valid"]},
            "objective": {"functions": {"init": "(context) -> state, commands",
                "update": "(context, state) -> state, commands",
                "migrate": "optional (context, state) -> state, commands"},
                "context": {"tick": "nonnegative int64", "generation": "host uint32 >0",
                    "state_version": "target schema 1..65535",
                    "previous_state_version": "migration only; prior schema",
                    "random": "host deterministic integer RNG function",
                    "events": "up to 64 ordered {tick, sequence, kind, id} records",
                    "timers": "with grant only; up to 16 due {tick, id}, deadline/id order"},
                "grant": "objective.hud requires init and update; objective.timers requires HUD",
                "state": {"values": ["boolean", "signed int64", "byte string (0..1024 bytes)", "table"],
                    "max_bytes": 16384, "max_depth": 16, "max_tables": 256,
                    "max_entries_per_table": 256, "record_keys": "1..63 bytes without NUL",
                    "tables": "dense arrays or records; no mixed keys, cycles or shared tables"},
                "commands": {"max_count": 32, "required": ["kind", "generation", "id"],
                    "kinds": {"hud.set": ["label", "value"], "hud.remove": [],
                              "timer.set": ["tick"], "timer.cancel": []}},
                "command_constraints": ["Generation must equal context.generation",
                    "Timer commands require objective.timers; tick > context.tick",
                    "Unique touched ID per batch in each namespace; at most 8 HUD rows / 16 timers",
                    "Any rejected command rolls back state, RNG, acknowledgment and resources"],
                "clock": "signed int64 120 Hz ticks; updates every 12 ticks; publish before T+1",
                "reference": "docs/PLUGIN_API.md"}}
