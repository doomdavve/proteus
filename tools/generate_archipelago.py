#!/usr/bin/env python3
"""Bake Skerry Reach, a synthetic, route-led archipelago, using only Python's stdlib.

Normal builds consume the checked-in default seed. Other seeds are offline experiments:
    python3 tools/generate_archipelago.py --seed 41718 --output build/skerry-41718
    python3 tools/generate_archipelago.py --self-test

All recipe coordinates are simulation X/Z; mesh X is reflected at emission. A shared
marching-triangle contour makes the terrain coast and compiled water exactly agree.
"""
import argparse
from collections import Counter, deque
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import map_pack  # noqa: E402

RECIPE = 2
DEFAULT_SEED = 41717
STEP = 32
BOUNDS = (-1600, -3104, 1600, 608)
WATER = (45, 79, 104)


def segment_distance(p, a, b):
    dx, dz = b[0] - a[0], b[1] - a[1]
    t = max(0, min(1, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dz)
                   / (dx * dx + dz * dz)))
    return math.hypot(p[0] - a[0] - t * dx, p[1] - a[1] - t * dz)


def conform_water(triangles):
    """Split neighbour edges at every clipping vertex, preventing false collision shores.

    A convex subtraction may split one triangle's edge beyond a pier corner while the
    adjacent triangle misses the pier. Indexed collision requires that split on both sides.
    The lattice buckets keep this local instead of scanning all vertices for every edge.
    """
    buckets = {}
    for triangle in triangles:
        for x,_,z in triangle:
            p = (round(x,4), 0, round(z,4))
            buckets.setdefault((math.floor(x/STEP), math.floor(z/STEP)), set()).add(p)
    result = []
    for triangle in triangles:
        ring = []
        changed = False
        for a,b in zip(triangle, triangle[1:]+triangle[:1]):
            ring.append(a)
            dx,dz = b[0]-a[0],b[2]-a[2]
            square = dx*dx+dz*dz
            if square < 1e-8:
                continue
            candidates = set()
            for iz in range(math.floor((min(a[2],b[2])-.001)/STEP),
                            math.floor((max(a[2],b[2])+.001)/STEP)+1):
                for ix in range(math.floor((min(a[0],b[0])-.001)/STEP),
                                math.floor((max(a[0],b[0])+.001)/STEP)+1):
                    candidates.update(buckets.get((ix,iz), ()))
            splits = []
            for p in candidates:
                t = ((p[0]-a[0])*dx+(p[2]-a[2])*dz)/square
                if not (1e-5 < t < 1-1e-5):
                    continue
                distance = abs(dx*(p[2]-a[2])-dz*(p[0]-a[0]))/math.sqrt(square)
                if distance < .00015:
                    splits.append((t,p))
            if splits:
                changed = True
                ring.extend(p for _,p in sorted(splits))
        if changed:
            center = (sum(p[0] for p in triangle)/3, 0,
                      sum(p[2] for p in triangle)/3)
            result.extend((center,a,b) for a,b in zip(ring,ring[1:]+ring[:1]))
        else:
            result.append(triangle)
    return result


class World:
    def __init__(self, seed):
        self.seed = seed
        self.rng = random.Random(seed)
        r = self.rng
        self.nodes = {
            "home": (0., 0.), "mouth": (0., -360.),
            "net_sheds": (-660 + r.uniform(-65, 65), -680 + r.uniform(-70, 70)),
            "narrows": (-150 + r.uniform(-45, 45), -1110 + r.uniform(-55, 55)),
            "beacon": (-490 + r.uniform(-65, 65), -1750 + r.uniform(-65, 65)),
            "lagoon": (840 + r.uniform(-55, 55), -1240 + r.uniform(-60, 60)),
            "east_sound": (690 + r.uniform(-60, 60), -1950 + r.uniform(-70, 70)),
            "outer_light": (90 + r.uniform(-60, 60), -2620 + r.uniform(-60, 60)),
        }
        # Protected routes are gently bent polylines, not straight canals cut through hills.
        # Their deliberately unequal lengths make short/narrow and long/open choices legible.
        self.routes = [
            ("home", "mouth", 38, []),
            ("mouth", "net_sheds", 54, [(-290, -455), (-520, -470)]),
            ("net_sheds", "narrows", 48, [(-700, -910), (-430, -1070)]),
            ("mouth", "narrows", 37, [(75, -650), (-40, -870)]),
            ("narrows", "beacon", 42, [(-190, -1390), (-430, -1510)]),
            ("narrows", "lagoon", 38, [(180, -1120), (455, -1390), (720, -1380)]),
            ("lagoon", "east_sound", 44, [(1030, -1440), (1000, -1730)]),
            ("beacon", "outer_light", 95, [(-620, -2110), (-360, -2440)]),
            ("beacon", "east_sound", 58, [(-60, -1890), (340, -1810)]),
            ("east_sound", "outer_light", 115, [(660, -2310), (410, -2520)]),
        ]
        self.corridors = []
        for a, b, width, bends in self.routes:
            points = [self.nodes[a], *bends, self.nodes[b]]
            self.corridors.extend((p, q, width) for p, q in zip(points, points[1:]))
        # Long ice-smoothed islands establish sheltered inner water and progressively
        # smaller, barer outer skerries. Each has its own contour phase and relief.
        recipe = [
            (0, 190, 530, 180, 30, 0),
            (-170, -120, 135, 265, 25, -.18), (175, -160, 140, 285, 29, .13),
            (-420, -740, 190, 205, 42, -.40), (365, -790, 205, 300, 64, -.22),
            (-1040, -730, 230, 390, 55, -.32), (-1040, -1550, 235, 335, 63, .21),
            (-510, -1270, 145, 165, 46, -.12), (120, -1460, 160, 180, 55, .30),
            (660, -1060, 175, 170, 37, .30), (1080, -1150, 155, 220, 44, -.1),
            (880, -1570, 125, 155, 27, .4), (520, -1550, 125, 155, 45, -.2),
            (-470, -1950, 140, 130, 31, .3), (-150, -2130, 130, 220, 32, -.4),
            (350, -2110, 155, 150, 31, .1), (1030, -2240, 175, 230, 39, -.3),
            (-990, -2390, 175, 205, 34, .3), (-350, -2810, 130, 110, 19, .1),
            (390, -2810, 135, 100, 22, -.2),
        ]
        self.islands = []
        for i, (x, z, rx, rz, height, angle) in enumerate(recipe):
            jitter = 0 if i < 3 else 42
            self.islands.append((x + r.uniform(-jitter, jitter), z + r.uniform(-jitter, jitter),
                                 rx * r.uniform(.90, 1.10), rz * r.uniform(.90, 1.10),
                                 height * r.uniform(.85, 1.15), angle + r.uniform(-.12, .12),
                                 r.uniform(0, 2 * math.pi)))
        # Small off-route skerries add near-water silhouettes without blocking the route graph.
        for _ in range(17):
            self.islands.append((r.uniform(-1300, 1300), r.uniform(-2820, -450),
                                 r.uniform(28, 65), r.uniform(35, 85), r.uniform(5, 17),
                                 r.uniform(-1, 1), r.uniform(0, 6.28)))
        # Give the trip distinct landforms, rather than a field of interchangeable domes.
        # The protected passages still determine the navigable openings through these lobes.
        nx, nz = self.nodes["narrows"]
        for side in (-1, 1):
            self.islands.append((nx + side * 94, nz - 28, 105, 175,
                                 102 if side < 0 else 76, side * .25, r.uniform(0, 6.28)))
        lx, lz = self.nodes["lagoon"]
        for i in range(8):
            angle = i * math.pi / 4
            self.islands.append((lx + math.cos(angle) * 175, lz + math.sin(angle) * 175,
                                 90, 100, r.uniform(24, 38), angle, r.uniform(0, 6.28)))
        ox, oz = self.nodes["outer_light"]
        self.islands.append((ox - 175, oz - 145, 125, 95, 58, -.45, 1.2))
        self.places = [
            ("HOME HARBOUR", "home", 65), ("NET SHEDS", "net_sheds", 70),
            ("NEEDLE PASS", "narrows", 60), ("BELL ROCK", "beacon", 70),
            ("HIDDEN LAGOON", "lagoon", 75), ("OUTER LIGHT", "outer_light", 90),
        ]
        self.vertices, self.triangles = [], []
        self.indices = {}
        self.shore = []
        self.docks = []
        self.sites = {}

    def field(self, x, z):
        result, peak = -10000., 0
        for cx, cz, rx, rz, height, angle, phase in self.islands:
            dx, dz = x - cx, z - cz
            ca, sa = math.cos(angle), math.sin(angle)
            u, v = (dx * ca + dz * sa) / rx, (-dx * sa + dz * ca) / rz
            theta = math.atan2(v, u)
            contour = 1 + .09 * math.sin(3 * theta + phase) + .045 * math.sin(7 * theta - phase)
            distance = (contour - math.hypot(u, v)) * min(rx, rz)
            if distance > result:
                result, peak = distance, height
        for a, b, width in self.corridors:
            result = min(result, segment_distance((x, z), a, b) - width)
        return result, peak

    def ground(self, x, z):
        d, peak = self.field(x, z)
        nx, nz = self.nodes["narrows"]
        gorge = math.hypot((x-nx) * .85, z-nz) < 240
        rise = 22 if gorge else 56 if z < -2050 else 75
        # Steep granite walls at Needle Pass, gentle fertile slopes inside the lagoon,
        # and low wind-scoured rock shelves at the exposed end of the archipelago.
        height = max(0, peak * (1 - math.exp(-max(0, d) / rise)))
        for node, (shore, _) in self.sites.items():
            if node not in ("net_sheds", "lagoon"):
                continue
            distance = math.hypot(x-shore[0], z-shore[1])
            blend = max(0, min(1, (distance-55)/55))
            terrace = min(3.2, max(0, d)*.28)
            height = terrace + (height-terrace)*blend
        return height

    def vertex(self, p):
        # Reflect simulation X to the renderer/map east axis. Round before deduplicating,
        # so each shared lattice/contour edge has exactly one collision vertex.
        point = tuple(round(v, 4) for v in (-p[0], p[1], p[2]))
        if point not in self.indices:
            self.indices[point] = len(self.vertices)
            self.vertices.append(point)
        return self.indices[point]

    def tri(self, points, color):
        ids = tuple(self.vertex(p) for p in points)
        # Clipping can leave collinear slivers on an exact contour/pier intersection.
        a,b,c = [self.vertices[i] for i in ids]
        flat_area = (b[2]-a[2])*(c[0]-a[0])-(b[0]-a[0])*(c[2]-a[2])
        if len(set(ids)) == 3 and not (a[1] == b[1] == c[1] == 0 and abs(flat_area) < 1e-7):
            self.triangles.append((ids, color))

    def quad(self, a, b, c, d, color):
        # Solids are described with outward simulation-space winding. X reflection
        # reverses their orientation, unlike terrain's explicitly upward mesh winding.
        self.tri((a, c, b), color)
        self.tri((a, d, c), color)

    def terrain(self):
        water, land = [], []
        x0, z0, x1, z1 = BOUNDS
        field = {(x, z): self.field(x, z)[0]
                 for z in range(z0, z1 + 1, STEP) for x in range(x0, x1 + 1, STEP)}
        # Overlapping island lobes can enclose tiny inland puddles. This is a sea voyage:
        # flood from the open boundary and turn unreachable lattice pockets into low rock.
        reachable = {p for p,value in field.items() if value <= 0 and
                     (p[0] in (x0,x1) or p[1] in (z0,z1))}
        pending = deque(reachable)
        while pending:
            x,z = pending.popleft()
            for dx,dz in ((STEP,0),(-STEP,0),(0,STEP),(0,-STEP),(STEP,STEP),(-STEP,-STEP)):
                p = x+dx,z+dz
                if p in field and field[p] <= 0 and p not in reachable:
                    reachable.add(p)
                    pending.append(p)
        self.filled_pockets = sum(value <= 0 and p not in reachable for p,value in field.items())
        for p,value in field.items():
            if value <= 0 and p not in reachable:
                field[p] = 2

        def clip(points, wet):
            out = []
            for a, b in zip(points, points[1:] + points[:1]):
                da, db = field[a], field[b]
                inside_a, inside_b = (da <= 0, db <= 0) if wet else (da > 0, db > 0)
                if inside_a:
                    out.append((a[0], 0 if wet else max(.5,self.ground(*a)), a[1]))
                if inside_a != inside_b:
                    # Canonical endpoint order prevents adjacent cells rounding differently.
                    first, second = sorted((a, b))
                    fa, fb = field[first], field[second]
                    t = fa / (fa - fb)
                    out.append((first[0] + t * (second[0] - first[0]), 0,
                                first[1] + t * (second[1] - first[1])))
            return out

        for z in range(z0, z1, STEP):
            for x in range(x0, x1, STEP):
                a, b, c, d = (x, z), (x + STEP, z), (x + STEP, z + STEP), (x, z + STEP)
                # Positive sim X/Z winding becomes upward after X reflection.
                for points in ([a, b, c], [a, c, d]):
                    for wet, target in ((True, water), (False, land)):
                        poly = clip(points, wet)
                        for i in range(1, len(poly) - 1):
                            target.append((poly[0], poly[i], poly[i + 1]))
        # Piers have actual collision footprints. Subtract their convex deck rectangle
        # before compiling water, then conform shared triangle edges after clipping.
        for start, end in self.docks:
            dx, dz = end[0]-start[0], end[1]-start[1]
            length = math.hypot(dx, dz)
            nx, nz = -dz/length*2.1, dx/length*2.1
            cutter = [(start[0]+nx, start[1]+nz), (end[0]+nx, end[1]+nz),
                      (end[0]-nx, end[1]-nz), (start[0]-nx, start[1]-nz)]
            cutter.reverse()  # Counterclockwise in simulation X/Z.
            new_water = []
            for triangle in water:
                polygon = list(triangle)
                if (max(p[0] for p in polygon) < min(p[0] for p in cutter)
                    or min(p[0] for p in polygon) > max(p[0] for p in cutter)
                    or max(p[2] for p in polygon) < min(p[1] for p in cutter)
                    or min(p[2] for p in polygon) > max(p[1] for p in cutter)):
                    new_water.append(triangle)
                    continue
                for a,b in zip(cutter, cutter[1:]+cutter[:1]):
                    inside, outside = [], []
                    def side(p):
                        return (b[0]-a[0])*(p[2]-a[1])-(b[1]-a[1])*(p[0]-a[0])
                    for p,q in zip(polygon, polygon[1:]+polygon[:1]):
                        dp,dq = side(p), side(q)
                        (inside if dp >= 0 else outside).append(p)
                        if (dp >= 0) != (dq >= 0):
                            t = dp/(dp-dq)
                            cross = (p[0]+t*(q[0]-p[0]), 0, p[2]+t*(q[2]-p[2]))
                            inside.append(cross)
                            outside.append(cross)
                    for i in range(1, len(outside)-1):
                        new_water.append((outside[0], outside[i], outside[i+1]))
                    polygon = inside
                    if len(polygon) < 3:
                        break
            water = new_water
        water = conform_water(water)
        for points in water:
            self.tri(points, WATER)
        self.water_count = len(self.triangles)
        for points in land:
            cx = sum(p[0] for p in points) / 3
            cz = sum(p[2] for p in points) / 3
            y = sum(p[1] for p in points) / 3
            # Broad weathered granite shore, heath above it, wooded inner islands.
            tone = int(5 * math.sin(cx / 180 + cz / 137))
            nx, nz = self.nodes["narrows"]
            if math.hypot((cx-nx) * .85, cz-nz) < 240:
                color = (132 + tone, 127 + tone, 117 + tone)
            elif y < 7 or cz < -1900:
                color = (115 + tone, 116 + tone, 111 + tone)
            elif y > 39:
                color = (102 + tone, 114 + tone, 88 + tone)
            else:
                color = (87 + tone, 108 + tone, 72 + tone)
            self.tri(points, color)
        self.terrain_end = len(self.triangles)

    def box(self, x, z, width, depth, y, height, color):
        a, b, c, d = (x-width/2, y, z-depth/2), (x+width/2, y, z-depth/2), \
            (x+width/2, y, z+depth/2), (x-width/2, y, z+depth/2)
        aa, bb, cc, dd = [(p[0], p[1]+height, p[2]) for p in (a, b, c, d)]
        self.quad(a, aa, bb, b, color)
        self.quad(b, bb, cc, c, tuple(max(0, k-9) for k in color))
        self.quad(c, cc, dd, d, color)
        self.quad(d, dd, aa, a, tuple(min(255, k+6) for k in color))
        self.quad(aa, dd, cc, bb, tuple(min(255, k+9) for k in color))

    def house(self, x, z, width=13, depth=9, color=(143, 58, 46)):
        corners = [(xx, zz) for xx in (x-width/2, x+width/2)
                   for zz in (z-depth/2, z+depth/2)]
        base = max(self.ground(xx, zz) for xx, zz in corners) + .2
        bottom = max(0, min(self.ground(xx, zz) for xx, zz in corners)-2)
        self.box(x, z, width + .5, depth + .5, bottom, base-bottom, (111, 109, 99))
        self.box(x, z, width, depth, base, 5, color)
        x0, x1, z0, z1 = x-width/2-.6, x+width/2+.6, z-depth/2-.6, z+depth/2+.6
        a, b, c, d = (x0, base+5, z0), (x1, base+5, z0), (x1, base+5, z1), (x0, base+5, z1)
        p, q = (x0, base+8, z), (x1, base+8, z)
        self.quad(a, p, q, b, (69, 73, 76))
        self.quad(p, d, c, q, (57, 61, 65))
        self.tri((a, p, d), color)
        self.tri((b, c, q), color)
        # White painted corners and window surrounds survive the game's low resolution.
        for xx in (x-width/2+.18, x+width/2-.18):
            self.box(xx, z, .34, depth+.05, base, 5.05, (209, 209, 186))
        for xx in (x-width*.26, x+width*.26):
            for zz in (z-depth/2-.06, z+depth/2+.06):
                self.box(xx, zz, 1.7, .13, base+2.2, 1.8, (214, 216, 192))
                self.box(xx, zz + (-.08 if zz < z else .08), 1.2, .10, base+2.45, 1.3,
                         (56, 77, 82))
        self.box(x+width*.25, z, 1, 1, base+6.5, 3, (104, 93, 81))

    def cone(self, x, z, base, radius, height, color, sides=6):
        tip = (x, base+height, z)
        for i in range(sides):
            a, b = 2*math.pi*i/sides, 2*math.pi*(i+1)/sides
            self.tri(((x+radius*math.cos(a), base, z+radius*math.sin(a)),
                      (x+radius*math.cos(b), base, z+radius*math.sin(b)), tip), color)

    def shore_at(self, node, bearing, distance=220):
        """First reliably dry point along a direction from a wet destination."""
        x, z = self.nodes[node]
        for d in range(20, distance + 1, 4):
            p = (x + math.cos(bearing)*d, z + math.sin(bearing)*d)
            if self.field(*p)[0] >= 16:
                return p
        return None

    def dock(self, start, end):
        x, z = start
        ex, ez = end
        length = math.hypot(ex-x, ez-z)
        steps = max(1, math.ceil(length/5))
        # Axis-free deck, shared by every pier segment, with timber piles to water level.
        dx, dz = (ex-x)/length, (ez-z)/length
        half = 2.1
        a, b = (x-dz*half, 1.6, z+dx*half), (ex-dz*half, 1.6, ez+dx*half)
        c, d = (ex+dz*half, 1.6, ez-dx*half), (x+dz*half, 1.6, z-dx*half)
        self.quad(a, b, c, d, (142, 119, 86))
        self.quad((a[0], -.7, a[2]), a, b, (b[0], -.7, b[2]), (100, 83, 62))
        self.quad((c[0], -.7, c[2]), c, d, (d[0], -.7, d[2]), (100, 83, 62))
        self.quad((d[0], -.7, d[2]), d, a, (a[0], -.7, a[2]), (100, 83, 62))
        self.quad((b[0], -.7, b[2]), b, c, (c[0], -.7, c[2]), (100, 83, 62))
        for i in range(steps+1):
            t = i/steps
            for side in (-1, 1):
                xx, zz = x+(ex-x)*t+side*dz*half, z+(ez-z)*t-side*dx*half
                self.box(xx, zz, .6, .6, -.7, 3.2, (94, 78, 59))

    def lighthouse(self, x, z, small=False):
        base = self.ground(x, z)
        h, w = (13, 3.5) if small else (29, 7)
        self.box(x, z, w+3, w+3, max(0, base-3), 3.7, (126, 122, 109))
        self.box(x, z, w, w, base+.5, h, (222, 220, 194))
        self.box(x, z, w+.5, w+.5, base+h*.65, h*.16, (158, 55, 43))
        self.box(x, z, w+1.8, w+1.8, base+h, .7, (56, 65, 68))
        self.box(x, z, w*.74, w*.74, base+h+.7, 2.7, (186, 183, 117))
        self.cone(x, z, base+h+3.4, w*.85, 2.8, (60, 67, 69), 8)

    def shore_workshop(self, x, z):
        """A visible working waterfront: lifting derrick, drying racks and timber stacks."""
        y = self.ground(x, z)
        self.box(x, z, 2.2, 2.2, y, 18, (155, 87, 47))
        self.box(x+5, z, 12, 1.1, y+17, 1.1, (166, 101, 54))
        self.box(x+10, z, .2, .2, y+7, 10, (61, 64, 60))
        for dz in (-15, 15):
            for dx in (-4, 4):
                yy = self.ground(x+dx, z+dz)
                self.box(x+dx, z+dz, .5, .5, yy, 5, (123, 100, 70))
            self.box(x, z+dz, 9, .6, y+4.6, .5, (134, 111, 78))
            self.box(x, z+dz, 7, .2, y+2, 2.5, (78, 92, 79))
        for dx, dz in ((-9, 8), (-12, 8), (-9, 12)):
            yy = self.ground(x+dx, z+dz)
            self.box(x+dx, z+dz, 2.5, 2.5, yy, 2, (150, 119, 75))

    def bell_house(self, x, z):
        base = self.ground(x, z)
        self.box(x, z, 9, 9, max(0, base-3), 3.5, (117, 111, 98))
        for dx in (-3, 3):
            for dz in (-3, 3):
                self.box(x+dx, z+dz, .8, .8, base, 14, (174, 167, 140))
        self.box(x, z, 8, 8, base+12, .8, (134, 66, 47))
        self.cone(x, z, base+12.8, 6.5, 5, (93, 110, 91), 4)
        self.cone(x, z, base+6, 2.5, 3, (154, 132, 67), 8)
        self.box(x, z, .5, .5, base+9, 3, (75, 78, 70))

    def plan_dressing(self):
        for x in (-110, 95):
            end = self.shore_at("home", math.atan2(-50, x), 220)
            if end:
                length = math.hypot(*end)
                start = (end[0]*(1-28/length), end[1]*(1-28/length))
                self.docks.append((start, end))
        for node, direction, count in (("net_sheds", math.pi, 4),
                                       ("lagoon", 0, 1), ("beacon", -math.pi/2, 1)):
            shore = self.shore_at(node, direction, 320)
            if not shore:
                # Prefer the nearest low shoreline rather than inventing a floating village.
                choices = [self.shore_at(node, j*math.pi/12, 320) for j in range(24)]
                shore = next((p for p in choices if p), None)
            if shore:
                nx, nz = self.nodes[node]
                sx, sz = shore
                d = math.hypot(sx-nx, sz-nz)
                start = (sx+(nx-sx)*min(28/d, .6), sz+(nz-sz)*min(28/d, .6))
                self.docks.append((start, shore))
                self.sites[node] = (shore, count)

    def dressing(self):
        r = self.rng
        self.structures = []
        # Home buildings sit on the head shore and face the sheltered launch water.
        for x,z in ((-100,-25),(-100,-70),(-100,-130),(100,-25),(100,-70),(100,-130)):
            shore = self.shore_at("home", math.atan2(z, x), 240)
            if shore:
                self.house(*shore, width=r.uniform(10, 16), color=r.choice(
                    [(151, 62, 46), (167, 143, 99), (188, 183, 153)]))
        mouth_beacon = self.shore_at("mouth", math.atan2(160,120), 300)
        if mouth_beacon:
            self.lighthouse(*mouth_beacon, small=True)
        for start, end in self.docks:
            self.dock(start, end)
        for node, (shore, count) in self.sites.items():
            sx,sz = shore
            for i in range(count):
                xx, zz = sx, sz+(i-(count-1)/2)*18
                if self.field(xx, zz)[0] > 8:
                    self.house(xx, zz, width=14 if count>1 else 10, depth=9,
                               color=(137, 57, 45) if count>1 else (122, 116, 87))
            self.structures.append({"place": node, "shore": list(shore)})
            if node == "net_sheds":
                self.shore_workshop(sx+4, sz+45)
                for i in range(3):
                    xx, zz = sx-28-i*12, sz+16+i*8
                    if self.field(xx, zz)[0] > 14:
                        self.house(xx, zz, width=18-i*2, depth=11,
                                   color=((178, 151, 103), (184, 182, 157), (147, 72, 49))[i])
            elif node == "lagoon":
                # A pale bathing hut, broadleaf canopy and a quiet landing contrast with
                # the industrial fishing sheds and the bare outer light.
                self.house(sx+14, sz+16, width=7, depth=6, color=(201, 187, 126))
        for node, small in (("beacon", True), ("outer_light", False)):
            choices = [self.shore_at(node, j*math.pi/12, 330) for j in range(24)]
            shore = next((p for p in choices if p), None)
            if shore:
                if small:
                    self.bell_house(*shore)
                    self.structures.append({"place": node, "bell_house": list(shore)})
                else:
                    self.lighthouse(*shore)
                    self.house(shore[0]-18, shore[1]+14, width=15, depth=10,
                               color=(186, 180, 149))
                    self.structures.append({"place": node, "lighthouse": list(shore)})
        # Trees cluster on fertile sheltered slopes; exposed outer rocks stay bare.
        self.tree_count = 0
        for _ in range(1800):
            x, z = r.uniform(-1350, 1350), r.uniform(-2100, 420)
            d, _ = self.field(x, z)
            y = self.ground(x, z)
            if d < 30 or y < 9 or y > 52:
                continue
            nx, nz = self.nodes["narrows"]
            if math.hypot((x-nx) * .85, z-nz) < 240:
                continue
            if any(math.hypot(x-s[0], z-s[1]) < 35 for s in
                   [tuple(v["shore"]) for v in self.structures if "shore" in v]):
                continue
            height = r.uniform(9, 18)
            self.box(x, z, .75, .75, y-.4, height*.6, (93, 79, 57))
            lx, lz = self.nodes["lagoon"]
            if math.hypot(x-lx, z-lz) < 370:
                self.cone(x, z, y+height*.25, height*.48, height*.55, (68, 96, 59), 8)
                self.cone(x, z, y+height*.55, height*.32, height*.45, (82, 110, 66), 8)
            else:
                self.cone(x, z, y+height*.25, height*.30, height*.70, (46, 74, 54))
                self.cone(x, z, y+height*.55, height*.20, height*.55, (55, 85, 59))
            self.tree_count += 1

    def preview(self, output):
        width, height = 800, 928
        def pos(p):
            # The in-game nautical chart has east to the right; simulation X increases west.
            return ((BOUNDS[2]-p[0])/4, (BOUNDS[3]-p[1])/4)
        lines = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
                 f'viewBox="0 0 {width} {height}">',
                 '<rect width="100%" height="100%" fill="#284f67"/>',
                 '<g stroke="none">']
        for ids, color in self.triangles[self.water_count:self.terrain_end]:
            points = [pos((-self.vertices[i][0], self.vertices[i][2])) for i in ids]
            coords = ' '.join(f'{x:.2f},{z:.2f}' for x, z in points)
            lines.append(f'<polygon points="{coords}" fill="rgb{color}"/>')
        lines.append('</g><g fill="none" stroke="#c8c28c" stroke-width="1.2" '
                     'stroke-dasharray="4 4" opacity=".7">')
        for a, b, _, bends in self.routes:
            points = [pos(p) for p in [self.nodes[a], *bends, self.nodes[b]]]
            lines.append('<polyline points="'+' '.join(f'{x:.1f},{z:.1f}' for x,z in points)+'"/>')
        lines.append('</g><g fill="#fff4cd" font-family="monospace" font-size="12">')
        for name, node, _ in self.places:
            x, z = pos(self.nodes[node])
            lines.append(f'<circle cx="{x}" cy="{z}" r="3"/><text x="{x+7}" '
                         f'y="{z-6}">{name}</text>')
        lines += [f'<text x="15" y="26">SKERRY REACH / SEED {self.seed}</text>',
                  '<text x="15" y="46">Synthetic coast; dashed lines are verified route samples</text>',
                  '</g></svg>']
        (output / "preview.svg").write_text('\n'.join(lines)+'\n', newline='\n')
        # A dependency-free raster companion lets review tools inspect the coastline too.
        pixels = [bytearray((40,79,103)*width) for _ in range(height)]
        for ids,color in self.triangles[self.water_count:self.terrain_end]:
            points = [pos((-self.vertices[i][0], self.vertices[i][2])) for i in ids]
            for iy in range(max(0, math.ceil(min(p[1] for p in points))),
                            min(height, math.ceil(max(p[1] for p in points)))):
                intersections = []
                for a,b in zip(points,points[1:]+points[:1]):
                    if min(a[1],b[1]) <= iy+.5 < max(a[1],b[1]):
                        t = (iy+.5-a[1])/(b[1]-a[1])
                        intersections.append(a[0]+t*(b[0]-a[0]))
                if len(intersections) == 2:
                    lo,hi = sorted(intersections)
                    lo,hi = max(0,math.ceil(lo-.5)),min(width,math.ceil(hi-.5))
                    pixels[iy][lo*3:hi*3] = bytes(color)*(hi-lo)
        for a,b,_,bends in self.routes:
            points = [pos(p) for p in [self.nodes[a],*bends,self.nodes[b]]]
            for p,q in zip(points,points[1:]):
                count = math.ceil(math.dist(p,q)*2)
                for i in range(count+1):
                    if i%16 < 8:
                        x,y = round(p[0]+(q[0]-p[0])*i/count),round(p[1]+(q[1]-p[1])*i/count)
                        if 0 <= x < width and 0 <= y < height:
                            pixels[y][x*3:x*3+3] = bytes((222,210,151))
        for _,node,_ in self.places:
            x,y = [round(v) for v in pos(self.nodes[node])]
            for dy in range(-3,4):
                for dx in range(-3,4):
                    if 0 <= x+dx < width and 0 <= y+dy < height:
                        pixels[y+dy][(x+dx)*3:(x+dx)*3+3] = bytes((255,245,205))
        def chunk(kind, data):
            return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
        png = b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',width,height,8,2,0,0,0))
        png += chunk(b'IDAT',zlib.compress(b''.join(b'\0'+row for row in pixels),9))
        (output/"preview.png").write_bytes(png+chunk(b'IEND',b''))

    def validate(self):
        # Independent sampled routes are checked against the actual emitted water triangles
        # below by test_generate_archipelago.py; this also validates the continuous recipe.
        for a, b, width, bends in self.routes:
            points = [self.nodes[a], *bends, self.nodes[b]]
            for p, q in zip(points, points[1:]):
                for i in range(math.ceil(math.dist(p, q)/8)+1):
                    t = i/max(1, math.ceil(math.dist(p, q)/8))
                    x, z = p[0]+t*(q[0]-p[0]), p[1]+t*(q[1]-p[1])
                    assert self.field(x, z)[0] <= -width+1e-6, (a, b, x, z)
        assert all(self.field(0, -z)[0] < -30 for z in range(0, 65, 4))
        # Shared edges occur twice inside water; once at actual coast/playable rectangle.
        edges = Counter(tuple(sorted((a,b))) for (ids, _) in self.triangles[:self.water_count]
                        for a,b in zip(ids, ids[1:]+ids[:1]))
        assert max(edges.values()) == 2
        self.shore_edges = [edge for edge, count in edges.items() if count == 1]
        assert len(self.shore_edges) > 100
        # Every water triangle has upward winding and nontrivial area after float32 bake.
        for ids, _ in self.triangles[:self.water_count]:
            a,b,c = [self.vertices[i] for i in ids]
            area = (b[2]-a[2])*(c[0]-a[0])-(b[0]-a[0])*(c[2]-a[2])
            assert area > 1e-7, (ids, area)

    def build(self):
        self.plan_dressing()
        self.terrain()
        self.dressing()
        self.validate()
        return self

    def write(self, output):
        output.mkdir(parents=True, exist_ok=True)
        blob = map_pack.write(output, "archipelago", "tools/generate_archipelago.py",
                              self.vertices, self.triangles, self.terrain_end)
        # map_pack's legacy default attribution names OSM; this recipe uses no real map data.
        header = (output/"water.h").read_text().replace(
            "// Map data (c) OpenStreetMap contributors, ODbL 1.0; see manifest.json.",
            "// Synthetic geometry; no external geographic sources. See manifest.json.")
        (output/"water.h").write_text(header, newline='\n')
        places = ['// Generated by tools/generate_archipelago.py. Do not edit.',
                  'static const BoatArchipelagoPlace archipelago_places[] = {']
        for name, node, radius in self.places:
            x,z = self.nodes[node]
            places.append(f'    {{ "{name}", {{ {map_pack.c_float(x)}, '
                          f'{map_pack.c_float(z)} }}, {map_pack.c_float(radius)} }},')
        (output/"places.h").write_text('\n'.join(places+['};', '']), newline='\n')
        routes = []
        route_points = []
        route_offsets = [0]
        for a,b,width,bends in self.routes:
            points = [self.nodes[a], *bends, self.nodes[b]]
            route_points.extend(points)
            route_offsets.append(len(route_points))
            routes.append({"from": a, "to": b, "minimum_recipe_half_width_m": width,
                           "length_m": round(sum(math.dist(p,q) for p,q in zip(points,points[1:])),1),
                           "points": points})
        route_lines = ['// Generated by tools/generate_archipelago.py. Simulation X/Z.',
                       'static const Vector2 archipelago_route_points[] = {']
        route_lines += [f'    {{ {map_pack.c_float(x)}, {map_pack.c_float(z)} }},'
                        for x,z in route_points]
        route_lines += ['};', 'static const unsigned archipelago_route_offsets[] = {',
                        '    '+', '.join(str(i) for i in route_offsets)+',', '};', '']
        (output/"routes.h").write_text('\n'.join(route_lines), newline='\n')
        manifest = {
            "name": "Skerry Reach", "location": "archipelago", "recipe": RECIPE,
            "seed": self.seed, "generator": "tools/generate_archipelago.py",
            "sources": "Entirely synthetic; no external geographic source data",
            "coordinates": "Metres; places/routes simulation X/Z; mesh X=-simulation X",
            "bounds_simulation_xz": BOUNDS, "lattice_step_m": STEP,
            "spawn": {"position": [0,0], "heading_radians": 0},
            "scenery_sha256": hashlib.sha256(blob).hexdigest(),
            "water_header_sha256": hashlib.sha256(header.encode()).hexdigest(),
            "counts": {"island_recipes": len(self.islands), "trees": self.tree_count,
                       "filled_inland_lattice_pockets": self.filled_pockets,
                       "vertices": len(self.vertices), "triangles": len(self.triangles),
                       "water_triangles": self.water_count, "shore_edges": len(self.shore_edges),
                       "scenery_bytes": len(blob)},
            "places": [{"name": name, "id": node, "position": self.nodes[node],
                        "arrival_radius_m": radius}
                       for name,node,radius in self.places],
            "routes": routes, "structures": self.structures,
            "dock_footprints": [{"start": a, "end": b, "width_m": 4.2} for a,b in self.docks],
            "limits": ["Bounded offline bake; outside rectangle is collision shore",
                       "Seed variation preserves the same region/route grammar",
                       "Coast and solid pier footprints supply static collision; land buildings are scenery",
                       "No bathymetry, tides, dynamic population or infinite streaming"],
        }
        for place in manifest["places"]:
            node = place["id"]
            target = next((item.get("lighthouse", item.get("bell_house", item.get("shore")))
                           for item in reversed(self.structures) if item["place"] == node), None)
            if node == "home":
                target = self.shore_at("home", math.atan2(-70,-100), 240)
            if target is None:
                target = self.shore_at(node, 0, 300) or self.shore_at(node, math.pi, 300)
            if target:
                x,z = self.nodes[node]
                # Review from a usable boating approach, not a distant graph waypoint.
                distance = math.hypot(target[0]-x,target[1]-z)
                stand_off = 250 if node == "outer_light" else 110
                t = max(0,1-stand_off/distance)
                while t > 0 and self.field(x+(target[0]-x)*t,z+(target[1]-z)*t)[0] > -20:
                    t = max(0,t-.05)
                cx,cz = x+(target[0]-x)*t,z+(target[1]-z)*t
                heading = math.atan2(target[0]-cx, -(target[1]-cz))
                place["capture"] = {"start": [cx,cz], "heading_degrees": round(math.degrees(heading),2),
                                    "target": target}
            if node == "narrows":
                # Look along the passage between its rock walls, rather than into a wall.
                x,z = self.nodes[node]
                target = self.routes[4][3][0]
                place["capture"] = {"start": [x,z],
                                    "heading_degrees": round(math.degrees(
                                        math.atan2(target[0]-x, -(target[1]-z))), 2),
                                    "target": target}
            elif node == "outer_light" and target:
                # The full headland and tower must fit above the default low helm camera.
                cx,cz = self.routes[9][3][-1]
                place["capture"] = {"start": [cx,cz],
                                    "heading_degrees": round(math.degrees(
                                        math.atan2(target[0]-cx, -(target[1]-cz))), 2),
                                    "target": target}
        (output/"manifest.json").write_text(json.dumps(manifest, indent=2)+'\n', newline='\n')
        self.preview(output)
        return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--output", type=Path, default=ROOT/"assets/geometry/archipelago")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        signatures = []
        for seed in (DEFAULT_SEED, DEFAULT_SEED+1, DEFAULT_SEED+2):
            with tempfile.TemporaryDirectory() as directory:
                world = World(seed).build()
                result = world.write(Path(directory))
                signatures.append(result["scenery_sha256"])
                print(f'seed={seed}: {result["counts"]}')
        assert len(set(signatures)) == 3
        print("archipelago_generator=ok seeds=3")
    else:
        result = World(args.seed).build().write(args.output)
        print(f'archipelago seed={args.seed}: {result["counts"]}')


if __name__ == "__main__":
    main()
