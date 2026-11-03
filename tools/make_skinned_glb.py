"""Generate a tiny two-bone skinned GLB with Idle/Walk clips.

The asset is intentionally simple: a quad whose top edge follows a rotating
tip bone. It is used to smoke-test tinygltf skinning and Animator crossfades
without shipping a large character.
"""
import json
import math
import struct
from pathlib import Path

out = Path(__file__).resolve().parents[1] / "assets" / "skinned_anim.glb"
out.parent.mkdir(parents=True, exist_ok=True)

# positions (x, y, z), normals, uv
positions = [
    (-0.5, 0.0, 0.0), (0.5, 0.0, 0.0),
    (0.5, 1.0, 0.0), (-0.5, 1.0, 0.0),
]
normals = [(0, 0, 1)] * 4
uvs = [(0, 1), (1, 1), (1, 0), (0, 0)]
joints = [(0, 0, 0, 0), (0, 0, 0, 0), (1, 0, 0, 0), (1, 0, 0, 0)]
weights = [(1, 0, 0, 0), (1, 0, 0, 0), (1, 0, 0, 0), (1, 0, 0, 0)]
indices = [0, 1, 2, 0, 2, 3]

# Identity inverse bind for bone 0, translate(0,-1,0) for the tip at y=1.
identity = [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]
tip_bind = [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,-1,0,1]

idle_times = [0.0, 1.0, 2.0]
idle_rots = [
    (0, 0, math.sin(0.05), math.cos(0.05)),
    (0, 0, math.sin(-0.05), math.cos(-0.05)),
    (0, 0, math.sin(0.05), math.cos(0.05)),
]
walk_times = [0.0, 0.5, 1.0]
walk_rots = [
    (0, 0, math.sin(0.35), math.cos(0.35)),
    (0, 0, math.sin(-0.35), math.cos(-0.35)),
    (0, 0, math.sin(0.35), math.cos(0.35)),
]

blob = bytearray()
views = []
accessors = []

def align():
    while len(blob) % 4:
        blob.append(0)

def add_view(data, target=None):
    align()
    off = len(blob)
    blob.extend(data)
    v = {"buffer": 0, "byteOffset": off, "byteLength": len(data)}
    if target:
        v["target"] = target
    views.append(v)
    return len(views) - 1

def add_accessor(view, comp_type, count, type_, minv=None, maxv=None, normalized=False):
    a = {"bufferView": view, "componentType": comp_type,
         "count": count, "type": type_}
    if minv is not None: a["min"] = minv
    if maxv is not None: a["max"] = maxv
    if normalized: a["normalized"] = True
    accessors.append(a)
    return len(accessors) - 1

pos_flat = [c for p in positions for c in p]
nrm_flat = [c for n in normals for c in n]
uv_flat = [c for t in uvs for c in t]
joint_flat = [c for j in joints for c in j]
weight_flat = [c for w in weights for c in w]

pos_view = add_view(struct.pack("<" + "f" * len(pos_flat), *pos_flat), 34962)
nrm_view = add_view(struct.pack("<" + "f" * len(nrm_flat), *nrm_flat), 34962)
uv_view = add_view(struct.pack("<" + "f" * len(uv_flat), *uv_flat), 34962)
joint_view = add_view(bytes(joint_flat), 34962)
weight_view = add_view(struct.pack("<" + "f" * len(weight_flat), *weight_flat), 34962)
idx_view = add_view(struct.pack("<" + "H" * len(indices), *indices), 34963)
ib_view = add_view(struct.pack("<" + "f" * 32, *(identity + tip_bind)))

pos_acc = add_accessor(pos_view, 5126, 4, "VEC3",
                       [-0.5, 0.0, 0.0], [0.5, 1.0, 0.0])
nrm_acc = add_accessor(nrm_view, 5126, 4, "VEC3")
uv_acc = add_accessor(uv_view, 5126, 4, "VEC2")
joint_acc = add_accessor(joint_view, 5121, 4, "VEC4")
weight_acc = add_accessor(weight_view, 5126, 4, "VEC4")
idx_acc = add_accessor(idx_view, 5123, len(indices), "SCALAR")
ib_acc = add_accessor(ib_view, 5126, 2, "MAT4")

def add_anim(name, times, rots):
    t_view = add_view(struct.pack("<" + "f" * len(times), *times))
    r_view = add_view(struct.pack("<" + "f" * len(rots) * 4,
                                  *[c for q in rots for c in q]))
    t_acc = add_accessor(t_view, 5126, len(times), "SCALAR",
                         [min(times)], [max(times)])
    r_acc = add_accessor(r_view, 5126, len(rots), "VEC4")
    return t_acc, r_acc

idle_t, idle_r = add_anim("Idle", idle_times, idle_rots)
walk_t, walk_r = add_anim("Walk", walk_times, walk_rots)

json_doc = {
    "asset": {"version": "2.0", "generator": "vlcrlib skinned_anim generator"},
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    "nodes": [
        {"name": "Body", "mesh": 0, "skin": 0, "children": [1]},
        {"name": "Root", "translation": [0, 0, 0], "children": [2]},
        {"name": "Tip", "translation": [0, 1, 0]},
    ],
    "meshes": [{
        "name": "SkinnedQuad",
        "primitives": [{
            "attributes": {
                "POSITION": pos_acc, "NORMAL": nrm_acc,
                "TEXCOORD_0": uv_acc, "JOINTS_0": joint_acc,
                "WEIGHTS_0": weight_acc,
            },
            "indices": idx_acc,
            "material": 0,
            "mode": 4,
        }],
    }],
    "materials": [{
        "name": "Skin",
        "pbrMetallicRoughness": {
            "baseColorFactor": [0.8, 0.25, 0.2, 1.0],
            "metallicFactor": 0.0,
            "roughnessFactor": 0.7,
        },
        "doubleSided": False,
    }],
    "skins": [{
        "name": "TwoBones",
        "inverseBindMatrices": ib_acc,
        "joints": [1, 2],
        "skeleton": 1,
    }],
    "animations": [
        {
            "name": "Idle",
            "samplers": [{"input": idle_t, "output": idle_r, "interpolation": "LINEAR"}],
            "channels": [{"sampler": 0, "target": {"node": 2, "path": "rotation"}}],
        },
        {
            "name": "Walk",
            "samplers": [{"input": walk_t, "output": walk_r, "interpolation": "LINEAR"}],
            "channels": [{"sampler": 0, "target": {"node": 2, "path": "rotation"}}],
        },
    ],
    "bufferViews": views,
    "accessors": accessors,
    "buffers": [{"byteLength": len(blob)}],
}

json_bytes = json.dumps(json_doc, separators=(",", ":")).encode("utf-8")
while len(json_bytes) % 4:
    json_bytes += b" "
while len(blob) % 4:
    blob.append(0)

total = 12 + 8 + len(json_bytes) + 8 + len(blob)
with out.open("wb") as f:
    f.write(struct.pack("<III", 0x46546C67, 2, total))
    f.write(struct.pack("<II", len(json_bytes), 0x4E4F534A))
    f.write(json_bytes)
    f.write(struct.pack("<II", len(blob), 0x004E4942))
    f.write(blob)

print(f"wrote {out} ({out.stat().st_size} bytes)")
