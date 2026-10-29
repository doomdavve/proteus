"""Machine-readable API 1 schemas. Native semantic validation remains authoritative.

JSON Schema describes a data projection of Lua records, not Lua functions or source syntax.
Cross-field geometry, ownership and callback/state rules are explicit native constraints.
"""
import math


def record(properties):
    return {"type": "object", "additionalProperties": False,
            "required": list(properties), "properties": properties}


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
                       "api": {"const": 1}, "kind": {"enum": ["voyage", "race"]},
                       "entry": {"const": "main.lua"}, "location": {"enum": ["outer", "river"]},
                       "capabilities": {"enum": [["voyage.define"],
                           ["voyage.define", "objective.hud"],
                           ["voyage.define", "objective.hud", "objective.timers"], ["race.define"]]},
                       "state_version": number(1, 65535, integer=True)})
    manifest["allOf"] = [
        {"if": {"properties": {"kind": {"const": "race"}}},
         "then": {"properties": {"location": {"const": "river"},
             "capabilities": {"const": ["race.define"]}, "state_version": {"const": 1}}},
         "else": {"properties": {"location": {"const": "outer"},
             "capabilities": {"not": {"const": ["race.define"]}}}}},
        {"if": {"properties": {"capabilities": {"const": ["voyage.define"]}}},
         "then": {"properties": {"state_version": {"const": 1}}}}]
    int64 = number(-(1 << 63), (1 << 63) - 1, integer=True)
    command_base = {"generation": number(1, 0xffffffff, integer=True), "id": text()}
    command_variants = []
    for kind, extra in (("hud.set", {"label": text(), "value": int64}), ("hud.remove", {}),
                        ("timer.set", {"tick": int64}), ("timer.cancel", {})):
        command_variants.append(record({"kind": {"const": kind}, **command_base, **extra}))
    commands = sequence({"oneOf": command_variants}, 0, 32)
    definitions = {"manifest": manifest, "voyage": voyage, "race": race, "commands": commands}
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
                "race": ["Gate directions have unit length within .01; source squared tolerance .0001",
                    "skip_ticks <= recovery_ticks; native course/rules validation",
                    "Grid, gate corridors and ferry path pass compiled-map clearance checks"]},
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
