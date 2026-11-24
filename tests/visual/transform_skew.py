"""Visual test: meshes drawn through skewed transforms look like the same meshes with the skew baked in.

    python tests/visual/transform_skew.py [--config RelWithDebInfo] [--port 27419] [--renderer NAME] [--keep-open]

Each case puts a mesh under a transform with skew (set locally, set in world space under a parent,
inherited from a skewed parent, a chain of skewed parents, a non-uniform parent with a rotated child,
a mirror) and, at the same place, a twin: the same mesh with that whole linear part baked into its
vertices and normals, under a transform that only moves it. A rigid case without skew sets the floor.
The expected matrices are computed here from position, Euler angles, scale and skew, independently of
the engine's transform class, and the skew each entity holds is read back from the editor.

The test starts an editor of its own on --port with a fresh project in the output folder, builds the
scene over MCP and captures each case's Scene view with the skewed meshes, then with the twins, for
each check in CHECKS: the normals debug view (geometry, culling, the normal transform), the lit image
without GI (direct light, shadows) and the lit image with GI (the GI's distance fields and cards).
A control case bakes its twin with a slightly different skew: it must fail, which shows the comparison
sees an error that small.

Output: <bin>/test_results/visual/transform_skew/ holds report.txt and, per case and check, a sheet
(skewed | baked | difference x8). Exit code 0 when every case passes and the control fails, 1 when
not, 2 when the test could not run.
"""

import argparse
import base64
import json
import math
import shutil
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import visual_check as vc  # noqa: E402

REPOSITORY = Path(__file__).resolve().parents[2]
OUTPUT_MARKER = ".visual_test_output"
RENDER_SIZE = (800, 600)
CASE_SPACING = 40.0
OBJECT_HEIGHT = 1.6
CAMERA_OFFSET = (-1.9, 1.3, -2.9)
# Frames each case renders lit before the first capture of a phase: GI starts from a new scene, or
# from none when it was just turned on.
WARM_UP_FRAMES = 120
# What each comparison captures: the Scene view's debug view, GI on or off, the frames rendered before
# the capture, and its limits: a pixel differs when a channel moves by more than threshold (0-255), and
# the check fails when more than the allowed fraction of the image's pixels differ.
CHECKS = [
    {"name": "normals", "view": "normals", "gi": False, "settle": 4, "threshold": 12, "allowed": 0.0010,
     "about": "the G-buffer's world normals: geometry, culling and the normal transform"},
    {"name": "lit", "view": "full", "gi": False, "settle": 60, "threshold": 16, "allowed": 0.0020,
     "about": "the lit image without GI: direct light, shadows, screen-space effects"},
    {"name": "gi", "view": "full", "gi": True, "settle": 90, "threshold": 16, "allowed": 0.0020,
     "about": "the lit image with GI: the GI's distance fields and cards of the mesh"},
]
CONTROL_SKEW_ERROR = (0.04, 0.0, 0.0)
# Every mesh draws with this one material, so the two meshes of a pair differ only by their transforms.
MATERIAL = "engine:/data/materials/standard.mat"


# Matrices: row-major 4 x 4 lists of lists

def identity():
    return [[1.0 if r == c else 0.0 for c in range(4)] for r in range(4)]


def mul(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def translation(p):
    m = identity()
    m[0][3], m[1][3], m[2][3] = p
    return m


def quat_from_euler_degrees(e):
    """glm::quat(vec3 euler radians), the conversion set_rotation_euler_degrees uses."""
    hx, hy, hz = (math.radians(v) * 0.5 for v in e)
    cx, cy, cz = math.cos(hx), math.cos(hy), math.cos(hz)
    sx, sy, sz = math.sin(hx), math.sin(hy), math.sin(hz)
    w = cx * cy * cz + sx * sy * sz
    x = sx * cy * cz - cx * sy * sz
    y = cx * sy * cz + sx * cy * sz
    z = cx * cy * sz - sx * sy * cz
    return w, x, y, z


def rotation(e):
    w, x, y, z = quat_from_euler_degrees(e)
    m = identity()
    m[0][0], m[0][1], m[0][2] = 1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)
    m[1][0], m[1][1], m[1][2] = 2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)
    m[2][0], m[2][1], m[2][2] = 2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)
    return m


def shear(k):
    """The upper unitriangular shear of a transform's skew (x, y, z): column 1 is (z, 1, 0), column 2 is (y, x, 1)."""
    m = identity()
    m[0][1] = k[2]
    m[0][2] = k[1]
    m[1][2] = k[0]
    return m


def scaling(s):
    m = identity()
    m[0][0], m[1][1], m[2][2] = s
    return m


def compose(position=(0, 0, 0), rotation_euler=(0, 0, 0), scale=(1, 1, 1), skew=(0, 0, 0)):
    """translation * rotation * shear * scale."""
    return mul(mul(mul(translation(position), rotation(rotation_euler)), shear(skew)), scaling(scale))


def linear(m):
    return [[m[r][c] for c in range(3)] for r in range(3)]


def det3(a):
    return (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
            a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))


def mul3(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(3)) for c in range(3)] for r in range(3)]


def apply3(a, v):
    return [a[r][0] * v[0] + a[r][1] * v[1] + a[r][2] * v[2] for r in range(3)]


def cofactor3(a):
    """The cofactor matrix: normals go through it (inverse transpose times the determinant)."""
    c = [[0.0] * 3 for _ in range(3)]
    for r in range(3):
        for k in range(3):
            r1, r2 = [i for i in range(3) if i != r]
            k1, k2 = [i for i in range(3) if i != k]
            c[r][k] = (-1) ** (r + k) * (a[r1][k1] * a[r2][k2] - a[r1][k2] * a[r2][k1])
    return c


def normalize(v):
    length = math.sqrt(sum(x * x for x in v))
    return [x / length for x in v]


# The mesh: a box and a sphere that overlaps it off-center, so each axis has its own extent

def make_shape():
    positions, normals, uvs, triangles = [], [], [], []
    faces = [((1, 0, 0), (0, 0, -1), (0, 1, 0)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
             ((0, 1, 0), (1, 0, 0), (0, 0, -1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
             ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (-1, 0, 0), (0, 1, 0))]
    for n, u, v in faces:
        base = len(positions)
        for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            positions.append([0.5 * (n[i] + su * u[i] + sv * v[i]) for i in range(3)])
            normals.append(list(n))
            uvs.append([(su + 1) * 0.5, (sv + 1) * 0.5])
        triangles += [(base, base + 1, base + 2), (base, base + 2, base + 3)]
    center, radius, rings, segments = (0.6, 0.75, 0.3), 0.35, 16, 24
    base = len(positions)
    for ring in range(rings + 1):
        theta = math.pi * ring / rings
        for segment in range(segments + 1):
            phi = 2.0 * math.pi * segment / segments
            n = [math.sin(theta) * math.cos(phi), math.cos(theta), math.sin(theta) * math.sin(phi)]
            positions.append([center[i] + radius * n[i] for i in range(3)])
            normals.append(n)
            uvs.append([segment / segments, ring / rings])
    for ring in range(rings):
        for segment in range(segments):
            a = base + ring * (segments + 1) + segment
            b = a + segments + 1
            # The first and last rings meet at a pole: only one triangle of each quad has an area there.
            if ring > 0:
                triangles.append((a, a + 1, b))
            if ring < rings - 1:
                triangles.append((a + 1, b + 1, b))
    return positions, normals, uvs, triangles


def bake(shape, file_linear):
    """The shape with a linear map applied in the file's space; normals through its cofactor matrix
    (sign kept for a mirror) and triangles reversed when it mirrors, so the faces still face out."""
    positions, normals, uvs, triangles = shape
    cofactor = cofactor3(file_linear)
    mirror = det3(file_linear) < 0.0
    sign = -1.0 if mirror else 1.0
    baked_positions = [apply3(file_linear, p) for p in positions]
    baked_normals = [normalize([sign * x for x in apply3(cofactor, n)]) for n in normals]
    baked_triangles = [(a, c, b) for a, b, c in triangles] if mirror else list(triangles)
    return baked_positions, baked_normals, uvs, baked_triangles


def inward_triangles(shape):
    """How many triangles wind against their vertex normals (counter-clockwise, seen from outside, is front)."""
    positions, normals, _, triangles = shape
    inward = 0
    for a, b, c in triangles:
        e1 = [positions[b][i] - positions[a][i] for i in range(3)]
        e2 = [positions[c][i] - positions[a][i] for i in range(3)]
        g = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
        inward += 1 if sum(g[i] * normals[a][i] for i in range(3)) <= 0.0 else 0
    return inward


def write_gltf(path: Path, shape) -> None:
    positions, normals, uvs, triangles = shape
    blob = bytearray()

    def add(values, fmt):
        offset = len(blob)
        blob.extend(struct.pack("<%d%s" % (len(values), fmt), *values))
        return offset, len(blob) - offset

    pos_offset, pos_length = add([x for p in positions for x in p], "f")
    nrm_offset, nrm_length = add([x for n in normals for x in n], "f")
    uv_offset, uv_length = add([x for t in uvs for x in t], "f")
    idx_offset, idx_length = add([i for t in triangles for i in t], "I")
    count = len(positions)
    document = {
        "asset": {"version": "2.0", "generator": "UnravelEngine visual test"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": path.stem, "mesh": 0}],
        "meshes": [{"name": path.stem, "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3}]}],
        "buffers": [{"byteLength": len(blob), "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(blob)).decode("ascii")}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": pos_offset, "byteLength": pos_length, "target": 34962},
            {"buffer": 0, "byteOffset": nrm_offset, "byteLength": nrm_length, "target": 34962},
            {"buffer": 0, "byteOffset": uv_offset, "byteLength": uv_length, "target": 34962},
            {"buffer": 0, "byteOffset": idx_offset, "byteLength": idx_length, "target": 34963},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": count, "type": "VEC3",
             "min": [min(p[i] for p in positions) for i in range(3)], "max": [max(p[i] for p in positions) for i in range(3)]},
            {"bufferView": 1, "componentType": 5126, "count": count, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": count, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5125, "count": len(triangles) * 3, "type": "SCALAR"},
        ],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document), encoding="ascii")


def file_to_engine_axes(file_min, file_max, engine_min, engine_max):
    """The signed axis permutation the importer applies (engine = C * file), read from the bounds of a
    shape whose axes all have different extents."""
    c = [[0.0] * 3 for _ in range(3)]
    for e in range(3):
        best = None
        for f in range(3):
            for s in (1.0, -1.0):
                lo, hi = (file_min[f], file_max[f]) if s > 0 else (-file_max[f], -file_min[f])
                error = abs(lo - engine_min[e]) + abs(hi - engine_max[e])
                if best is None or error < best[0]:
                    best = (error, f, s)
        if best[0] > 1e-3:
            raise vc.VisualTestError("the imported bounds %s..%s are no axis permutation of %s..%s" % (engine_min, engine_max, file_min, file_max))
        c[e][best[1]] = best[2]
    return c


# The cases. Each entity: name, parent (an earlier entity of the case), space, and the transform
# channels to set; the last entity carries the mesh. Positions of root entities are offsets from the
# case's origin.

CASES = [
    {"name": "rigid", "about": "no skew: rotation and uniform scale, the floor every other case is held to",
     "entities": [{"name": "mesh", "space": "local", "position": (0, OBJECT_HEIGHT, 0), "rotation_euler": (20, 35, -10),
                   "scale": (1.2, 1.2, 1.2)}]},
    {"name": "local_skew", "about": "skew set on the mesh's own transform, with rotation and non-uniform scale",
     "entities": [{"name": "mesh", "space": "local", "position": (0, OBJECT_HEIGHT, 0), "rotation_euler": (20, 35, -10),
                   "scale": (1.2, 0.8, 1.0), "skew": (0.35, -0.25, 0.4)}]},
    {"name": "world_skew_under_parent", "about": "skew set in world space on a mesh whose parent is rotated and scaled",
     "entities": [{"name": "parent", "space": "local", "position": (0.3, OBJECT_HEIGHT - 0.2, 0.2), "rotation_euler": (0, 30, 15),
                   "scale": (1.5, 1.5, 1.5)},
                  {"name": "mesh", "parent": "parent", "space": "world", "position": (0, OBJECT_HEIGHT, 0),
                   "rotation_euler": (-15, 50, 5), "scale": (0.9, 1.1, 1.2), "skew": (-0.3, 0.2, 0.35)}]},
    {"name": "skewed_parent", "about": "a mesh without skew under a skewed, rotated, non-uniform parent",
     "entities": [{"name": "parent", "space": "local", "position": (0, OBJECT_HEIGHT, 0), "rotation_euler": (10, -25, 0),
                   "scale": (1.0, 1.3, 0.9), "skew": (0.3, 0.2, -0.35)},
                  {"name": "mesh", "parent": "parent", "space": "local", "position": (0.2, 0.1, -0.1),
                   "rotation_euler": (10, -40, 25), "scale": (0.8, 0.8, 0.8)}]},
    {"name": "skew_chain", "about": "skew at every level of a three-level hierarchy",
     "entities": [{"name": "grandparent", "space": "local", "position": (0, OBJECT_HEIGHT, 0), "rotation_euler": (0, 20, 0),
                   "scale": (1.1, 0.9, 1.0), "skew": (0.2, 0.0, 0.15)},
                  {"name": "parent", "parent": "grandparent", "space": "local", "position": (0.1, 0.0, 0.1),
                   "rotation_euler": (15, 0, -20), "scale": (0.9, 1.1, 1.0), "skew": (0.0, -0.25, 0.1)},
                  {"name": "mesh", "parent": "parent", "space": "local", "position": (-0.1, 0.05, 0.0),
                   "rotation_euler": (0, -30, 10), "scale": (1.0, 0.9, 1.1), "skew": (0.15, 0.1, -0.2)}]},
    {"name": "inherited_shear", "about": "a rotated mesh under a non-uniform parent: the shear comes from the hierarchy alone",
     "entities": [{"name": "parent", "space": "local", "position": (0, OBJECT_HEIGHT, 0), "scale": (2.0, 0.6, 1.0)},
                  {"name": "mesh", "parent": "parent", "space": "local", "rotation_euler": (0, 45, 30), "scale": (0.7, 0.7, 0.7)}]},
    {"name": "mirrored_skew", "about": "skew with a negative scale, which turns the faces inside out unless culling follows",
     "entities": [{"name": "mesh", "space": "local", "position": (0, OBJECT_HEIGHT, 0), "rotation_euler": (5, -20, 10),
                   "scale": (-1.0, 1.1, 0.9), "skew": (0.2, 0.3, -0.25)}]},
    {"name": "mirrored", "about": "a negative scale without skew, for comparison with mirrored_skew",
     "entities": [{"name": "mesh", "space": "local", "position": (0, OBJECT_HEIGHT, 0), "rotation_euler": (5, -20, 10),
                   "scale": (-1.0, 1.1, 0.9)}]},
    {"name": "control", "control": True,
     "about": "local_skew with its twin baked at a skew off by %s: must fail" % (CONTROL_SKEW_ERROR,),
     "entities": [{"name": "mesh", "space": "local", "position": (0, OBJECT_HEIGHT, 0), "rotation_euler": (20, 35, -10),
                   "scale": (1.2, 0.8, 1.0), "skew": (0.35, -0.25, 0.4)}]},
]


def case_origin(index):
    return (CASE_SPACING * index, 0.0, 0.0)


def offset(p, origin):
    return tuple(p[i] + origin[i] for i in range(3))


def expected_global(case, origin, wrong_skew=None):
    """The mesh's global matrix, from the channels the case sets."""
    globals_by_name = {}
    for spec in case["entities"]:
        skew = spec.get("skew", (0, 0, 0))
        if wrong_skew is not None and spec is case["entities"][-1]:
            skew = tuple(skew[i] + wrong_skew[i] for i in range(3))
        position = spec.get("position", (0, 0, 0))
        parent = spec.get("parent")
        if spec["space"] == "world" or parent is None:
            position = offset(position, origin)
        local = compose(position, spec.get("rotation_euler", (0, 0, 0)), spec.get("scale", (1, 1, 1)), skew)
        if spec["space"] == "world" or parent is None:
            globals_by_name[spec["name"]] = local
        else:
            globals_by_name[spec["name"]] = mul(globals_by_name[parent], local)
    return globals_by_name[case["entities"][-1]["name"]]


# The run

def prepare_output(folder: Path) -> None:
    if folder.exists():
        if not (folder / OUTPUT_MARKER).exists():
            raise vc.VisualTestError("%s exists and is not a visual test's output; pass another --output" % folder)
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    (folder / OUTPUT_MARKER).write_text("Written by tests/visual/transform_skew.py; deleted at its next run.\n")


def close_scene_prompt(editor: vc.Editor) -> None:
    status = editor.call("editor_get_status")
    modal = status.get("modal") or {}
    if modal.get("title") == "Create Scene":
        editor.call("editor_answer_modal", {"button": "Cancel"})


def prepare_scene(editor: vc.Editor) -> None:
    """A new scene from the low preset without Temporal AA and without the reflection probe.

    Temporal AA jitters each frame by its index, and the two captures of a pair are taken on other
    frames. The preset's probe captures the scene's objects on demand, so it would show the skewed
    meshes' state in the twins' captures."""
    close_scene_prompt(editor)
    editor.call("scene_new_from_preset", {"preset": "low", "force": True})
    for entity in editor.call("scene_find_entities", {"component": "Temporal AA"}).get("entities", []):
        editor.call("scene_remove_components_batch", {"items": [{"entity_id": entity["id"], "component": "Temporal AA"}]})
    probes = editor.call("scene_find_entities", {"component": "Reflection Probe"}).get("entities", [])
    if probes:
        editor.call("scene_delete_entities_batch", {"entity_ids": [entity["id"] for entity in probes]})


def set_gi(editor: vc.Editor, enabled: bool) -> None:
    """Turns the scene volume's Global Illumination on or off."""
    for entity in editor.call("scene_find_entities", {"component": "Global Illumination"}).get("entities", []):
        editor.call("scene_set_component_properties_batch",
                    {"items": [{"entity_id": entity["id"], "component": "Global Illumination", "properties": {"enabled": enabled}}]})


def import_meshes(editor: vc.Editor, files: list) -> None:
    result = editor.call("assets_import_batch", {"paths": [str(f) for f in files], "folder": "skew", "wait_ms": 60000})
    not_ready = [item for item in result.get("results", []) if item.get("asset") and item.get("state") != "ready"]
    if not_ready or result.get("ready") != len(files):
        raise vc.VisualTestError("meshes did not import: %s" % result)


def mesh_key(name: str) -> str:
    return "app:/data/skew/%s.gltf" % name


def calibrate_axes(editor: vc.Editor, shape) -> list:
    info = editor.call("assets_get_mesh_info", {"key": mesh_key("shape")})
    bounds = info.get("bounds") or info
    positions = shape[0]
    file_min = [min(p[i] for p in positions) for i in range(3)]
    file_max = [max(p[i] for p in positions) for i in range(3)]
    return file_to_engine_axes(file_min, file_max, bounds["min"], bounds["max"])


def create_entities(editor: vc.Editor, items: list) -> list:
    return [entry["id"] for entry in editor.call("scene_create_batch", {"items": items})["created"]]


def build_case(editor: vc.Editor, case, origin, ids):
    """Creates the case's ground, its skewed entities and its twin; returns (skewed root ids, twin id)."""
    create_entities(editor, [{"kind": "primitive", "primitive": "plane", "name": case["name"] + "_ground",
                              "position": list(origin), "scale": [1.6, 1, 1.6]}])
    by_name = {}
    roots = []
    for spec in case["entities"]:
        is_mesh = spec is case["entities"][-1]
        item = {"kind": "mesh" if is_mesh else "empty", "name": case["name"] + "_" + spec["name"]}
        if is_mesh:
            item["asset"] = mesh_key("shape")
            item["material"] = MATERIAL
        if spec.get("parent"):
            item["parent_id"] = by_name[spec["parent"]]
        entity = create_entities(editor, [item])[0]
        by_name[spec["name"]] = entity
        if not spec.get("parent"):
            roots.append(entity)
        pose = {"entity_id": entity, "space": spec["space"]}
        position = spec.get("position", (0, 0, 0))
        if spec["space"] == "world" or not spec.get("parent"):
            position = offset(position, origin)
        # Every channel is set, so nothing depends on where the editor puts a new entity.
        pose["position"] = list(position)
        pose["rotation_euler"] = list(spec.get("rotation_euler", (0, 0, 0)))
        pose["scale"] = list(spec.get("scale", (1, 1, 1)))
        pose["skew"] = list(spec.get("skew", (0, 0, 0)))
        editor.call("scene_set_transforms_batch", {"items": [pose]})
    ids[case["name"]] = by_name
    return roots


def read_back_skews(editor, cases, ids) -> dict:
    """Per case, the entities whose skew, read back in the space it was set in, is not the one set."""
    entity_ids = [ids[case["name"]][spec["name"]] for case in cases for spec in case["entities"]]
    poses = {pose["id"]: pose for pose in editor.call("scene_get_entities_batch", {"entity_ids": entity_ids, "detail": "pose"})["entities"]}
    mismatches = {}
    for case in cases:
        for spec in case["entities"]:
            pose = poses[ids[case["name"]][spec["name"]]]
            field = "skew" if spec["space"] == "world" else "skew_local"
            read = pose.get(field, [0.0, 0.0, 0.0])
            wanted = spec.get("skew", (0, 0, 0))
            if max(abs(read[i] - wanted[i]) for i in range(3)) > 1e-4:
                mismatches.setdefault(case["name"], []).append("%s %s reads %s, set %s" % (spec["name"], field, read, list(wanted)))
    return mismatches


def twin_name(case):
    return "twin_" + case["name"]


def build_twins(editor, cases, axes, shape, mesh_folder):
    """Writes and imports one baked mesh per case and creates its twin, which only moves."""
    files = []
    placements = {}
    axes_inverse = [[axes[c][r] for c in range(3)] for r in range(3)]
    for index, case in enumerate(cases):
        origin = case_origin(index)
        m = expected_global(case, origin, CONTROL_SKEW_ERROR if case.get("control") else None)
        file_linear = mul3(mul3(axes_inverse, linear(m)), axes)
        baked = bake(shape, file_linear)
        if inward_triangles(baked):
            raise vc.VisualTestError("%s: the baked twin's faces point inward" % case["name"])
        path = mesh_folder / (twin_name(case) + ".gltf")
        write_gltf(path, baked)
        files.append(path)
        placements[case["name"]] = (m[0][3], m[1][3], m[2][3])
    import_meshes(editor, files)
    twins = {}
    for case in cases:
        twins[case["name"]] = create_entities(editor, [{"kind": "mesh", "asset": mesh_key(twin_name(case)), "name": twin_name(case),
                                                        "material": MATERIAL, "position": list(placements[case["name"]])}])[0]
    return twins, placements


def set_active(editor, ids, active):
    editor.call("scene_set_active_batch", {"items": [{"entity_id": i, "active": active} for i in ids]})


def capture_cases(editor, cases, placements, view, settle_frames):
    editor.call("viewport_set_debug_view", {"mode": view})
    images = {}
    for case in cases:
        target = placements[case["name"]]
        editor.call("viewport_set_camera", {"position": list(offset(CAMERA_OFFSET, target)), "target": list(target)})
        images[case["name"]] = editor.capture("viewport_capture_scene", {"settle_frames": settle_frames})
    return images


def capture_all(editor, cases, placements, skewed_roots, twins) -> dict:
    """captures[(state, check name)][case name], state "skewed" or "baked"; GI off first, then on."""
    captures = {}
    for gi in (False, True):
        set_gi(editor, gi)
        set_active(editor, skewed_roots, True)
        set_active(editor, twins, False)
        capture_cases(editor, cases, placements, "full", WARM_UP_FRAMES)
        for state in ("skewed", "baked"):
            set_active(editor, skewed_roots, state == "skewed")
            set_active(editor, twins, state == "baked")
            for check in CHECKS:
                if check["gi"] == gi:
                    captures[(state, check["name"])] = capture_cases(editor, cases, placements, check["view"], check["settle"])
    return captures


def run(arguments) -> int:
    bin_dir = vc.find_bin_dir(REPOSITORY, arguments.config)
    output = Path(arguments.output) if arguments.output else bin_dir / "test_results" / "visual" / "transform_skew"
    prepare_output(output)
    project = output / "project"
    project.mkdir()
    mesh_folder = output / "meshes"

    shape = make_shape()
    if inward_triangles(shape):
        raise vc.VisualTestError("the test shape's faces point inward")
    write_gltf(mesh_folder / "shape.gltf", shape)

    extra = ["--renderer", arguments.renderer] if arguments.renderer else []
    report = []
    with vc.Editor(bin_dir, project, arguments.port, output / "editor_output.txt", extra) as editor:
        prepare_scene(editor)
        import_meshes(editor, [mesh_folder / "shape.gltf"])
        axes = calibrate_axes(editor, shape)

        ids = {}
        skewed_roots = []
        for index, case in enumerate(CASES):
            skewed_roots += build_case(editor, case, case_origin(index), ids)
        twins, placements = build_twins(editor, CASES, axes, shape, mesh_folder)
        skew_mismatches = read_back_skews(editor, CASES, ids)

        editor.call("selection_set", {"entity_ids": []})
        editor.call("viewport_set_overlays", {"gizmos": False, "icons": False, "grid": False, "selection_outline": False,
                                              "selection_wireframe": False})
        editor.call("viewport_set_render_size", {"width": RENDER_SIZE[0], "height": RENDER_SIZE[1], "panel": "scene"})

        captures = capture_all(editor, CASES, placements, skewed_roots, list(twins.values()))
        editor.call("viewport_set_debug_view", {"mode": "full"})
        editor.call("scene_save", {"key": "app:/data/transform_skew.spfb"})
        errors = [entry for entry in editor.call("logs_get_recent", {"min_level": "error", "max_count": 50}).get("entries", [])]
        if arguments.keep_open:
            input("The editor stays open on port %d; press Enter to stop it." % arguments.port)

    failures = 0
    for case in CASES:
        lines = []
        case_failed = False
        transform_failed = bool(skew_mismatches.get(case["name"]))
        for mismatch in skew_mismatches.get(case["name"], []):
            lines.append("    transform FAIL  %s" % mismatch)
        for check in CHECKS:
            skewed = captures[("skewed", check["name"])][case["name"]]
            baked = captures[("baked", check["name"])][case["name"]]
            result = vc.compare(skewed, baked, check["threshold"])
            check_failed = result.differing_fraction > check["allowed"]
            case_failed |= check_failed
            vc.write_png(output / ("%s_%s.png" % (case["name"], check["name"])),
                         vc.side_by_side([skewed, baked, vc.difference_image(skewed, baked, 8)]))
            lines.append("    %-8s %s  %.4f%% of pixels differ by more than %d (allowed %.2f%%), largest %d, mean %.3f" % (
                check["name"], "FAIL" if check_failed else "pass", 100.0 * result.differing_fraction, check["threshold"],
                100.0 * check["allowed"], result.max_difference, result.mean_difference))
        if transform_failed:
            verdict = "FAIL (the editor does not hold the skew that was set)"
            failures += 1
        elif case.get("control"):
            verdict = "pass (the control differs, as it must)" if case_failed else "FAIL (the control's error was not seen)"
            failures += 0 if case_failed else 1
        else:
            verdict = "FAIL" if case_failed else "pass"
            failures += 1 if case_failed else 0
        report.append("%s: %s\n  %s\n%s" % (case["name"], verdict, case["about"], "\n".join(lines)))

    report.append("editor errors logged: %d" % len(errors))
    for entry in errors:
        report.append("  " + json.dumps(entry)[:300])
    failures += 1 if errors else 0
    report.append("RESULT: %s" % ("pass" if failures == 0 else "%d failure(s)" % failures))
    text = "\n".join(report)
    (output / "report.txt").write_text(text + "\n", encoding="ascii", errors="replace")
    print(text)
    print("images and report: %s" % output)
    return 0 if failures == 0 else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--config", default="RelWithDebInfo", help="the configured tree whose editor runs (build/<config>/bin)")
    parser.add_argument("--port", type=int, default=27419, help="the MCP port of the test's own editor; never another session's")
    parser.add_argument("--renderer", default="", help="the editor's --renderer, e.g. vulkan or d3d11; default the editor's")
    parser.add_argument("--output", default="", help="where the project, images and report go; default <bin>/test_results/visual/transform_skew")
    parser.add_argument("--keep-open", action="store_true", help="leave the editor running until Enter, to look at the scene")
    arguments = parser.parse_args()
    started = time.time()
    try:
        code = run(arguments)
    except vc.VisualTestError as error:
        print("ERROR: %s" % error)
        code = 2
    print("took %.0f s" % (time.time() - started))
    return code


if __name__ == "__main__":
    sys.exit(main())
