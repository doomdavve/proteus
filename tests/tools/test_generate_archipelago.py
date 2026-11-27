#!/usr/bin/env python3
"""Validate baked topology, route choices and reproducibility independently of the field."""
from collections import Counter, defaultdict, deque
import hashlib
import json
import math
from pathlib import Path
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import generate_archipelago as generator  # noqa: E402
import map_pack  # noqa: E402


class Water:
    def __init__(self, vertices, triangles):
        self.vertices = [(-p[0], p[2]) for p in vertices]
        self.triangles = triangles
        self.cells = defaultdict(list)
        self.shore_cells = defaultdict(list)
        edges = defaultdict(list)
        for i,(ids,_) in enumerate(triangles):
            points = [self.vertices[j] for j in ids]
            for cell in self.box_cells(points):
                self.cells[cell].append(i)
            for a,b in zip(ids,ids[1:]+ids[:1]):
                edges[tuple(sorted((a,b)))].append(i)
        assert all(len(owners) <= 2 for owners in edges.values()), 'Non-manifold water edge'
        self.shore = [edge for edge,owners in edges.items() if len(owners) == 1]
        degrees = Counter(i for edge in self.shore for i in edge)
        assert set(degrees.values()) == {2}, 'Unstitched water contour or clipping T-junction'
        for edge in self.shore:
            points = [self.vertices[i] for i in edge]
            for cell in self.box_cells(points, 16):
                self.shore_cells[cell].append(points)
        adjacency = defaultdict(list)
        for owners in edges.values():
            if len(owners) == 2:
                a,b = owners
                adjacency[a].append(b)
                adjacency[b].append(a)
        seen, pending = {0}, deque([0])
        while pending:
            for neighbour in adjacency[pending.popleft()]:
                if neighbour not in seen:
                    seen.add(neighbour)
                    pending.append(neighbour)
        assert len(seen) == len(triangles), 'Disconnected water component'

    @staticmethod
    def box_cells(points, margin=0):
        for iz in range(math.floor((min(p[1] for p in points)-margin)/64),
                        math.floor((max(p[1] for p in points)+margin)/64)+1):
            for ix in range(math.floor((min(p[0] for p in points)-margin)/64),
                            math.floor((max(p[0] for p in points)+margin)/64)+1):
                yield ix,iz

    def contains(self, point, clearance=0):
        cell = math.floor(point[0]/64),math.floor(point[1]/64)
        contained = False
        for index in self.cells[cell]:
            ids,_ = self.triangles[index]
            points = [self.vertices[i] for i in ids]
            crosses = [(b[0]-a[0])*(point[1]-a[1])-(b[1]-a[1])*(point[0]-a[0])
                       for a,b in zip(points,points[1:]+points[:1])]
            if min(crosses) >= -.0001 or max(crosses) <= .0001:
                contained = True
                break
        return contained and all(generator.segment_distance(point,a,b) >= clearance
                                 for a,b in self.shore_cells[cell])


def traffic_samples(actor):
    """Sample the emitted rounded route, including its actual circular corners."""
    points = actor['points']
    corners = []
    for i,p in enumerate(points):
        a,b = points[i-1],points[(i+1)%len(points)]
        before,after = math.dist(a,p),math.dist(p,b)
        d0 = ((p[0]-a[0])/before,(p[1]-a[1])/before)
        d1 = ((b[0]-p[0])/after,(b[1]-p[1])/after)
        turn = math.atan2(d0[0]*d1[1]-d0[1]*d1[0],d0[0]*d1[0]+d0[1]*d1[1])
        radius = actor['radius']
        tangent = radius*math.tan(abs(turn)/2)
        assert tangent < min(before,after)
        enter = (p[0]-d0[0]*tangent,p[1]-d0[1]*tangent)
        leave = (p[0]+d1[0]*tangent,p[1]+d1[1]*tangent)
        side = 1 if turn > 0 else -1
        center = (enter[0]-d0[1]*radius*side,enter[1]+d0[0]*radius*side)
        angle = math.atan2(enter[1]-center[1],enter[0]-center[0])
        count = max(1,math.ceil(radius*abs(turn)/4))
        for j in range(count+1):
            phase = angle+turn*j/count
            yield center[0]+radius*math.cos(phase),center[1]+radius*math.sin(phase)
        corners.append((enter,leave))
    for i,(_,start) in enumerate(corners):
        end = corners[(i+1)%len(corners)][0]
        count = max(1,math.ceil(math.dist(start,end)/4))
        for j in range(count+1):
            yield start[0]+(end[0]-start[0])*j/count,start[1]+(end[1]-start[1])*j/count


def check_seed(seed, output):
    world = generator.World(seed).build()
    manifest = world.write(output)
    vertices,triangles,water,bridge = map_pack.read_pack(output/'scenery.bin')
    assert water == manifest['counts']['water_triangles']
    assert bridge == world.terrain_end
    assert hashlib.sha256((output/'scenery.bin').read_bytes()).hexdigest() == manifest['scenery_sha256']
    mesh = Water(vertices,triangles[:water])
    assert len(mesh.shore) == manifest['counts']['shore_edges']
    preview = ET.parse(output/'preview.svg')
    marks = preview.findall('.//{http://www.w3.org/2000/svg}circle')
    assert len(marks) == len(manifest['places'])
    # Each marker must lie on the same side of home as on the game's north-up chart.
    for mark, place in zip(marks, manifest['places']):
        x, z = place['position']
        east = float(mark.get('cx')) - float(marks[0].get('cx'))
        north = float(marks[0].get('cy')) - float(mark.get('cy'))
        assert math.isclose(east, -x / 4, abs_tol=.06)
        assert math.isclose(north, z / 4, abs_tol=.06)
    for z in range(0,-65,-4):
        assert mesh.contains((0,z), 12), ('launch',seed,z)
    for place in manifest['places']:
        assert mesh.contains(place['position'],12), ('place',seed,place)
        if 'capture' in place:
            assert mesh.contains(place['capture']['start'],12), ('capture',seed,place)
    for route in manifest['routes']:
        for a,b in zip(route['points'],route['points'][1:]):
            count = math.ceil(math.dist(a,b)/8)
            for i in range(count+1):
                t = i/count
                p = a[0]+t*(b[0]-a[0]), a[1]+t*(b[1]-a[1])
                assert mesh.contains(p,12), ('route',seed,route['from'],route['to'],p)
    assert manifest['lattice_step_m'] == 16 and manifest['open_water_step_m'] == 32
    assert len(manifest['life']['actors']) == 4 and manifest['life']['gulls'] == 26
    for actor in manifest['life']['actors']:
        for p in traffic_samples(actor):
            assert mesh.contains(p,8), ('traffic',seed,actor['id'],p)
    assert len(manifest['marina']) == 4
    for berth in manifest['marina']:
        h = berth['heading']
        length, beam = (7, 2.4) if berth['recipe'] == 'SAILBOAT_7M' else (5.8, 2.15)
        for side in (-1, 0, 1):
            for end in (-1, 0, 1):
                across, along = side*(beam/2+.45), end*(length/2+.9)
                p = (berth['x']-math.cos(h)*across-math.sin(h)*along,
                     berth['z']-math.sin(h)*across+math.cos(h)*along)
                assert mesh.contains(p,.1), ('marina hull over shore/pier',seed,berth,p)
        assert abs(berth['x']) > 20, ('marina launch clearance',seed,berth)
    assert len(manifest['life']['residents']) >= 4
    for resident in manifest['life']['residents']:
        a,b = resident['start'],resident['end']
        for i in range(17):
            p = a[0]+(b[0]-a[0])*i/16,a[2]+(b[2]-a[2])*i/16
            assert not mesh.contains(p), ('resident over water',seed,p)
            assert not any(abs(p[0]-x) < width/2+1 and abs(p[1]-z) < depth/2+1
                           for x,z,width,depth in world.buildings), ('resident in building',seed,p)
    # A second way through the inner islands must be materially longer, not a duplicate
    # segment over the same water. The separating island itself must be solid.
    routes = {(r['from'],r['to']):r for r in manifest['routes']}
    direct = routes['mouth','narrows']['length_m']
    alternate = routes['mouth','net_sheds']['length_m']+routes['net_sheds','narrows']['length_m']
    assert alternate > direct*1.25
    cx,cz,*_ = world.islands[3]
    assert not mesh.contains((cx,cz)), 'Island separating inner routes must remain solid'
    # Pier decks meet the collision surface. Test from the water end, beyond the end cap.
    assert len(manifest['dock_footprints']) >= 3
    for pier in manifest['dock_footprints']:
        a,b = pier['start'],pier['end']
        t = min(3/math.dist(a,b),.4)
        p = a[0]+t*(b[0]-a[0]), a[1]+t*(b[1]-a[1])
        assert not mesh.contains(p), ('ghost pier',seed,p)
    assert 'OpenStreetMap' not in (output/'water.h').read_text()
    print(f'seed={seed} route_clearance=12m topology=connected piers=solid '
          f'inner_routes={direct:.0f}/{alternate:.0f}m pack={manifest["counts"]["scenery_bytes"]}B')
    return manifest


def check_house_seams():
    """Probe the wall/gable junction and overhang from inside, including both windings."""
    def sub(a,b):
        return tuple(x-y for x,y in zip(a,b))
    def cross(a,b):
        return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
    def dot(a,b):
        return sum(x*y for x,y in zip(a,b))
    def hit(world, origin, direction):
        for ids,_ in world.triangles:
            a,b,c = (world.vertices[i] for i in ids)
            ab,ac = sub(b,a),sub(c,a)
            p = cross(direction,ac)
            det = dot(ab,p)
            if abs(det) < 1e-9:
                continue
            offset = sub(origin,a)
            u = dot(offset,p)/det
            q = cross(offset,ab)
            v,t = dot(direction,q)/det,dot(ac,q)/det
            if u >= -1e-7 and v >= -1e-7 and u+v <= 1+1e-7 and t > 0:
                return True
        return False
    for width,depth in ((7,6),(13,9),(16,10.5)):
        world = generator.World(generator.DEFAULT_SEED)
        world.ground = lambda x,z: 0
        world.house(0,0,width,depth)
        eave = .15+(3.8 if width < 11 else 4.4)
        for side in (-1,1):
            for fraction in (-.98,-.5,0,.5,.98):
                for dy in (-.01,0,.01):
                    assert hit(world,(0,eave+dy,fraction*depth/2),(side,0,0)), \
                        ('open wall/gable seam',width,depth,side,fraction,dy)
            for end in (-1,1):
                assert hit(world,(side*(width/2+.25),eave-1,end*(depth/2+.25)),(0,1,0)), \
                    ('open roof underside',width,depth,side,end)
    print('house_seams=ok sizes=3 gables=both overhangs=all')


def main():
    check_house_seams()
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        manifests = [check_seed(seed,root/str(seed)) for seed in
                     (generator.DEFAULT_SEED,generator.DEFAULT_SEED+1,generator.DEFAULT_SEED+2)]
        assert len({m['scenery_sha256'] for m in manifests}) == 3
        repeated = generator.World(generator.DEFAULT_SEED).build()
        repeated.write(root/'repeat')
        for path in (root/str(generator.DEFAULT_SEED)).iterdir():
            assert path.read_bytes() == (root/'repeat'/path.name).read_bytes(), path.name
            shipped = ROOT/'assets/geometry/archipelago'/path.name
            if path.suffix == '.bin':
                # Compression can differ between zlib versions; the geometry must agree.
                generated_mesh = map_pack.read_pack(path)
                shipped_mesh = map_pack.read_pack(shipped)
                assert generated_mesh[1:] == shipped_mesh[1:], 'Stale default mesh topology'
                assert len(generated_mesh[0]) == len(shipped_mesh[0])
                for generated, stored in zip(generated_mesh[0], shipped_mesh[0]):
                    assert all(abs(a-b) <= .0002 for a,b in zip(generated,stored)), \
                        'Stale default mesh vertex'
            elif path.suffix == '.json':
                generated, stored = json.loads(path.read_text()), json.loads(shipped.read_text())
                assert (generated['recipe'],generated['seed']) == (stored['recipe'],stored['seed'])
                assert generated['routes'] == stored['routes']
                assert generated['places'] == stored['places']
            elif path.suffix != '.png':
                assert path.read_text() == shipped.read_text(), f'Stale default bake: {path.name}'
    print('archipelago_generator=ok seeds=3 repeat=identical shipped=geometry_matched')


if __name__ == '__main__':
    main()
