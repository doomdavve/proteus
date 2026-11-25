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


def main():
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
