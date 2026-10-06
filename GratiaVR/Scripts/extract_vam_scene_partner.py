"""Partner body and viewpoint of a VaM scene, in the performance space of the retargeted character.

Reads the VaM scene JSON (static Person atom = the viewer/partner), the controller samples of the
performing atom (frame 0 fixes the same calibration as author_vam_mocap.py: Unity -> Blender
M, start yaw, hip centre, SCALE) and writes partner aims in the partner mesh's component space,
the partner component transform and the partner eye viewpoint in the character actor space
(Unreal cm; Blender -> Unreal is (x, -y, z) x 100). Pure data; run with system Python:

    python GratiaVR/Scripts/extract_vam_scene_partner.py <scene.json> <samples.json> <out.json>

The partner mesh is assumed to face +Y with Z up in its component space (Epic mannequin).
"""
import json
import math
import sys
from pathlib import Path

import numpy as np

SCALE = 0.955 / 0.932  # author_vam_mocap.py: Gratia leg length / VaM leg length
M = np.array([[-1.0, 0.0, 0.0], [0.0, 0.0, -1.0], [0.0, 1.0, 0.0]])  # Unity -> Blender
F = np.diag([1.0, -1.0, 1.0])  # Blender -> Unreal axes (FBX -Y forward, Z up)
P = np.array([[-1.0, 0.0, 0.0], [0.0, 0.0, 1.0], [0.0, 1.0, 0.0]])  # mannequin CS -> Unity local (x=-X, y=Z, z=Y)


def rot_x(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def rot_y(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def rot_z(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def unity_euler(e):
    """Unity Quaternion.Euler(x, y, z): z, then x, then y."""
    x, y, z = (math.radians(float(e[k])) for k in "xyz")
    return rot_y(y) @ rot_x(x) @ rot_z(z)


def unity_quat(q):
    x, y, z, w = q
    n = math.sqrt(x * x + y * y + z * z + w * w)
    x, y, z, w = x / n, y / n, z / n, w / n
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def vec(d):
    return np.array([float(d["x"]), float(d["y"]), float(d["z"])])


scene_path, samples_path, out_path = (Path(a) for a in sys.argv[1:4])
scene = json.loads(scene_path.read_text(encoding="utf-8-sig"))
samples = json.loads(samples_path.read_text(encoding="utf-8"))
atoms = {a["id"]: a for a in scene["atoms"]}
performer = atoms[samples["target_atom"]]
partner = next(a for a in scene["atoms"] if a.get("type") == "Person" and a["id"] != samples["target_atom"] and a.get("on") == "true")

# Calibration of the performer (author_vam_mocap.py): frame 0 hip faces -Y, hips over the origin.
hip0 = samples["frames"][0]["controllers"]["hipControl"]
hip_rot = M @ unity_quat(hip0["quaternion_xyzw"]) @ M.T
hip_pos = M @ np.array(hip0["position_xyz"])
facing = hip_rot @ np.array([0.0, -1.0, 0.0])
yaw = math.atan2(facing[0], -facing[1])
ALIGN = rot_z(-yaw)
start_hips = hip_pos + hip_rot @ (M @ np.array([0.0, -0.10, -0.04]))
CENTER = np.array([start_hips[0], start_hips[1], 0.0])

# Controllers are local to each atom's container.
perf_R, perf_t = unity_euler(performer["containerRotation"]), vec(performer["containerPosition"])
part_R, part_t = unity_euler(partner["containerRotation"]), vec(partner["containerPosition"])


def to_actor_point(unity_local_partner):
    world = part_R @ unity_local_partner + part_t
    performer_local = perf_R.T @ (world - perf_t)
    blender = SCALE * (ALIGN @ (M @ performer_local - CENTER))
    return 100.0 * (F @ blender)


def to_actor_dir(unity_local_dir):
    d = F @ ALIGN @ M @ perf_R.T @ part_R @ unity_local_dir
    return d / np.linalg.norm(d)


controls = {s["id"]: s for s in partner["storables"] if s.get("id", "").endswith("Control") and "localPosition" in s}
local = {k: vec(v["localPosition"]) for k, v in controls.items()}
mannequin = {k: (P.T @ v) * 100.0 for k, v in local.items()}  # partner component space, cm

# Partner rotation (mannequin CS -> actor) and translation.
linear = F @ ALIGN @ M @ perf_R.T @ part_R @ P
assert abs(np.linalg.det(linear) - 1.0) < 1e-6, "partner transform must be a proper rotation"
origin = to_actor_point(np.zeros(3))
w = math.sqrt(max(0.0, 1.0 + linear[0, 0] + linear[1, 1] + linear[2, 2])) / 2.0
quat = [(linear[2, 1] - linear[1, 2]) / (4 * w), (linear[0, 2] - linear[2, 0]) / (4 * w), (linear[1, 0] - linear[0, 1]) / (4 * w), w]


def mid(a, b):
    return (mannequin[a] + mannequin[b]) * 0.5


aims = [("pelvis", "spine_01", "hipControl", "abdomenControl", True)]
chain = [("spine_01", "spine_02", "abdomenControl", "abdomen2Control"), ("spine_02", "spine_03", "abdomen2Control", "chestControl"),
         ("spine_03", "spine_04", "chestControl", "neckControl"), ("spine_04", "spine_05", "chestControl", "neckControl"),
         ("spine_05", "neck_01", "chestControl", "neckControl"), ("neck_01", "neck_02", "neckControl", "headControl"),
         ("neck_02", "head", "neckControl", "headControl")]
for side, prefix in (("l", "l"), ("r", "r")):
    chain += [("clavicle_" + side, "upperarm_" + side, prefix + "ShoulderControl", prefix + "ArmControl"),
              ("upperarm_" + side, "lowerarm_" + side, prefix + "ArmControl", prefix + "ElbowControl"),
              ("lowerarm_" + side, "hand_" + side, prefix + "ElbowControl", prefix + "HandControl"),
              ("thigh_" + side, "calf_" + side, prefix + "ThighControl", prefix + "KneeControl"),
              ("calf_" + side, "foot_" + side, prefix + "KneeControl", prefix + "FootControl"),
              ("foot_" + side, "ball_" + side, prefix + "FootControl", prefix + "ToeControl")]
aims += [(b, c, f, t, False) for b, c, f, t in chain]
out_aims = [{"bone": b, "child": c, "from": mannequin[f].round(3).tolist(), "to": mannequin[t].round(3).tolist(), "place": p,
             "source": [f, t]} for b, c, f, t, p in aims]

# Eyes: 8 cm above and 9 cm in front of the head control (VaM head = skull base), head-space.
head_R = unity_euler(controls["headControl"]["localRotation"])
eye_local = local["headControl"] + head_R @ np.array([0.0, 0.08, 0.09])
look = to_actor_dir(head_R @ np.array([0.0, 0.0, 1.0]))
up = to_actor_dir(head_R @ np.array([0.0, 1.0, 0.0]))
up = up - look * float(look @ up)
up /= np.linalg.norm(up)
eye = to_actor_point(eye_local)
head_actor = to_actor_point(local["headControl"])
hip_actor = to_actor_point(local["hipControl"])

out = {
    "schema": 1, "source_scene": str(scene_path), "partner_atom": partner["id"], "performer_atom": performer["id"],
    "calibration": {"scale": SCALE, "yaw_degrees": math.degrees(yaw), "center_m": CENTER.tolist()},
    "partner_transform": {"location_cm": origin.round(3).tolist(), "quat_xyzw": [round(v, 6) for v in quat]},
    "aims": out_aims,
    "viewpoint": {"location_cm": eye.round(3).tolist(), "x_axis": look.round(6).tolist(), "z_axis": up.round(6).tolist()},
    "check": {"partner_head_cm": head_actor.round(2).tolist(), "partner_hip_cm": hip_actor.round(2).tolist()},
}
out_path.write_text(json.dumps(out, indent=2), encoding="utf-8")
print(json.dumps({k: out[k] for k in ("calibration", "partner_transform", "viewpoint", "check")}, indent=1))
