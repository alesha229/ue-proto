"""Retarget VaM Timeline controller targets (KM466, first 10 s) onto Gratia; Blender MCP only.

Supply this source to execute_blender_code (GratiaVR/Scripts/Blender-MCP.py code ...); it never
launches Blender. Input: evidence/05/kitty_mocap/trial10s_samples.json (extract_vam_timeline.py).

Calibration (measured on all 601 samples, see docs/MOCAP_KM466.md): VaM controllers are world
aligned in the VaM T-pose (identity = bone in T-pose, character facing Unity +Z). So
    bone_world(t) = controller(t) x bone orientation in Gratia's T-pose.
Unity (left-handed, Y up) -> Blender (right-handed, Z up, facing -Y): p_b = (-x, -z, y),
R_b = M R_u M^T. Knee/elbow controllers sit on the joints; VaM hip joints are 9 cm lateral,
10 cm below and 4 cm behind hipControl. Leg length 0.932 m vs Gratia 0.955 m -> scale 1.024.

Legs: thigh/shin aim along the source hip->knee->ankle segments with the knee hinge in the
bend plane (no twist); one global height offset puts the lowest point of the clip on the floor.
Arms: wrist targets relative to the chest (scaled), two-bone solve with Gratia's lengths, elbow
controller as the bend-plane pole (a natural pole where the controller is not on the elbow).
Spine/head/feet/hands: controller rotations. Fingers: VaM bend angles minus Gratia's rest
bend. Face: approximate mapping of the ARKit-like channels to Gratia's shape keys.

PHASE "preview": pose a few times, render with red markers at the scaled source joints.
PHASE "author": key the source rig, bake the game rig per frame, validate against the source
evaluated meshes (whole and half frames), save the editable copy.
PHASE "export": export the validated clip (game rig + candidate meshes for morph curves).
"""
import bpy
import hashlib
import json
import math
import time
from pathlib import Path

import numpy as np
from mathutils import Euler, Matrix, Quaternion, Vector

ROOT = Path("E:/coding/ue proto")
OUT = ROOT / "Exports/Gratia/GameRig"
# LONG = True: the whole take (566.9 s) in 60 s segments with one shared corrective basis.
LONG = globals().get("LONG", False)
EVIDENCE = ROOT / ("evidence/05/kitty_mocap_full" if LONG else "evidence/05/kitty_mocap_v3")
EVIDENCE.mkdir(parents=True, exist_ok=True)
SAMPLES = ROOT / ("evidence/05/kitty_mocap/trial566.9s_samples.json" if LONG else "evidence/05/kitty_mocap/trial10s_samples.json")
PHASE = globals().get("PHASE", "preview")
PREVIEW_TIMES = globals().get("PREVIEW_TIMES", [0.0, 2.5, 5.0, 7.5, 10.0])
CLIP = "KM466Full" if LONG else "KM466"
STEM = CLIP.lower()
FPS = 30
DURATION = 566.9 if LONG else 10.0
SEGMENT_FRAMES = 1800
SCALE = 0.955 / 0.932
assert Path(bpy.data.filepath).resolve() == (ROOT / "Exports/Gratia/Gratia_mvp.blend").resolve()
for filename, expected in [
    ("Gratia.blend", "B4C98285167829C824194E5E5A9F8D4BE14AAE7C377C2CCE4E37F19BC4B88DC7"),
    ("Gratia_source.blend", "B4C98285167829C824194E5E5A9F8D4BE14AAE7C377C2CCE4E37F19BC4B88DC7"),
    ("Gratia_working.blend", "C455FD7983B1360DAC1006E8B549B91263CC7396EDC98E9D2A1B72B506A4F9A5"),
]:
    with (ROOT / filename).open("rb") as stream:
        assert hashlib.file_digest(stream, "sha256").hexdigest().upper() == expected, filename

started = time.time()
source = bpy.data.objects["Gratia"]
game = bpy.data.objects["Gratia_GameRig"]
scene = bpy.context.scene
assert source.matrix_world == Matrix.Identity(4), "source rig must sit at the origin"
manifest = json.loads((OUT / "game_rig_manifest.json").read_text(encoding="utf-8"))
pairs = [(bpy.data.objects[item["source"]], bpy.data.objects[item["candidate"]]) for item in manifest["meshes"]]
report_path = EVIDENCE / ("%s_validation.json" % STEM)
data = json.loads(SAMPLES.read_text(encoding="utf-8"))
RATE = data["sample_rate_hz"]
COUNT = data["frame_count"]
assert COUNT == int(round(data["duration_seconds"] * RATE)) + 1 and abs(data["duration_seconds"] - DURATION) < 1e-6
frames = data["frames"]
PB = source.pose.bones

# ---------------------------------------------------------------- source conversion
M = Matrix(((-1.0, 0.0, 0.0), (0.0, 0.0, -1.0), (0.0, 1.0, 0.0)))
MT = M.transposed()


def unity_rotation(q):
    x, y, z, w = q
    return M @ Quaternion((w, x, y, z)).normalized().to_matrix() @ MT


def unity_point(p):
    return M @ Vector(p)


def raw(index):
    sample = frames[index]["controllers"]
    return {name: (unity_point(c["position_xyz"]), unity_rotation(c["quaternion_xyzw"])) for name, c in sample.items()}


def hip_joint_offset(sign):
    """VaM hip joint in hipControl space (Unity: left = -X): 9 cm lateral, 10 cm down, 4 cm back."""
    return M @ Vector((sign * 0.09, -0.10, -0.04))


first = raw(0)
facing = first["hipControl"][1] @ Vector((0.0, -1.0, 0.0))
yaw = math.atan2(facing.x, -facing.y)
ALIGN = Matrix.Rotation(-yaw, 3, "Z")
start_hips = (first["hipControl"][0] + first["hipControl"][1] @ ((hip_joint_offset(-1) + hip_joint_offset(1)) * 0.5))
CENTER = Vector((start_hips.x, start_hips.y, 0.0))
assert abs((ALIGN @ facing).x) < 1e-6 and (ALIGN @ facing).y < 0


def aligned(index):
    """Source targets at a 60 Hz sample: aligned to face -Y at the start, hips above the origin."""
    out = {}
    for name, (point, rotation) in raw(index).items():
        out[name] = (ALIGN @ (point - CENTER), ALIGN @ rotation)
    hip_point, hip_rot = out["hipControl"]
    for side, sign in (("L", -1), ("R", 1)):
        out["hipJoint." + side] = (hip_point + hip_rot @ hip_joint_offset(sign), hip_rot)
    return out


def sample_at(t):
    index = min(COUNT - 1, max(0, int(round(t * RATE))))
    return index, aligned(index)


# Elbow controllers that are not on the elbow (right arm, first 3 s) give no bend plane.
ELBOW_LENGTH = {"L": 0.237, "R": 0.247}
elbow_weight = {}
for side, prefix in (("L", "l"), ("R", "r")):
    weights = []
    for f in frames:
        c = f["controllers"]
        d = (Vector(c[prefix + "HandControl"]["position_xyz"]) - Vector(c[prefix + "ElbowControl"]["position_xyz"])).length
        error = abs(d - ELBOW_LENGTH[side])
        weights.append(1.0 - min(1.0, max(0.0, (error - 0.015) / 0.015)))
    width = int(0.5 * RATE) | 1
    kernel = np.ones(width) / width
    padded = np.pad(np.array(weights), width // 2, mode="edge")
    elbow_weight[side] = np.convolve(padded, kernel, mode="valid").tolist()

# ---------------------------------------------------------------- Gratia rest calibration
REST = {name: PB[name].bone.matrix_local.to_3x3() for name in PB.keys()}


def rest_head(name):
    return PB[name].bone.head_local.copy()


HIPS_REST = (rest_head("thigh_fk.L") + rest_head("thigh_fk.R")) * 0.5
TORSO_PIVOT_REST = rest_head("torso")
HIP_CONTROL_REST = HIPS_REST + M @ Vector((0.0, 0.10, 0.04)) * SCALE
CHEST_REF_REST = HIP_CONTROL_REST + Vector((0.0, 0.0, 0.196 * SCALE))
CHEST_REF_LOCAL = source.data.bones["DEF-spine.003"].matrix_local.inverted() @ CHEST_REF_REST


def hand_t_pose(side):
    """Hand orientation in a T-pose (arm horizontal, palm down) from Gratia's A-pose rest."""
    rest = REST["hand_fk." + side]
    along = rest.col[1].normalized()
    lateral = Vector((1.0 if side == "L" else -1.0, 0.0, 0.0))
    return along.rotation_difference(lateral).to_matrix() @ rest


HAND_T = {side: hand_t_pose(side) for side in ("L", "R")}


def hinge_sign(upper, lower):
    a = (PB[lower].bone.head_local - PB[upper].bone.head_local).normalized()
    b = (PB[lower].bone.tail_local - PB[lower].bone.head_local).normalized()
    cross = a.cross(b)
    return 1.0 if cross.dot(REST[upper].col[0]) >= 0 else -1.0


HINGE = {key: hinge_sign(*key) for key in [("upper_arm_fk.L", "forearm_fk.L"), ("upper_arm_fk.R", "forearm_fk.R"),
                                           ("thigh_fk.L", "shin_fk.L"), ("thigh_fk.R", "shin_fk.R")]}

FINGERS = ["index", "middle", "ring", "pinky"]


def finger_bones(finger, side):
    return ["f_%s.0%d.%s" % (finger, k, side) for k in (1, 2, 3)]


def palm_bone(finger, side):
    number = {"index": "01", "middle": "02", "ring": "03", "pinky": "04"}[finger]
    return "ORG-palm.%s.%s" % (number, side)


def rest_bend(parent_dir, bone):
    child = REST[bone].col[1].normalized()
    axis = REST[bone].col[0].normalized()
    p = (parent_dir - axis * parent_dir.dot(axis)).normalized()
    c = (child - axis * child.dot(axis)).normalized()
    return math.degrees(math.atan2(p.cross(c).dot(axis), p.dot(c)))


FINGER_REST = {}
for side in ("L", "R"):
    for finger in FINGERS:
        names = finger_bones(finger, side)
        parent = source.data.bones[palm_bone(finger, side)].matrix_local.to_3x3().col[1].normalized() \
            if palm_bone(finger, side) in source.data.bones else REST["hand_fk." + side].col[1].normalized()
        dirs = [parent] + [REST[n].col[1].normalized() for n in names[:-1]]
        FINGER_REST.update({n: rest_bend(d, n) for d, n in zip(dirs, names)})


def frame_set(frame):
    scene.frame_set(math.floor(frame), subframe=frame % 1)
    bpy.context.view_layer.update()
    return bpy.context.evaluated_depsgraph_get()


def reset_pose():
    for bone in PB:
        bone.location = Vector()
        bone.scale = Vector((1, 1, 1))
        if bone.rotation_mode == "QUATERNION":
            bone.rotation_quaternion = Quaternion()
        elif bone.rotation_mode == "AXIS_ANGLE":
            bone.rotation_axis_angle = (0.0, 0.0, 1.0, 0.0)
        else:
            bone.rotation_euler = Euler()
    bpy.context.view_layer.update()


def set_world(name, rotation3, location=None):
    bone = PB[name]
    where = bone.matrix.translation.copy() if location is None else Vector(location)
    bone.matrix = Matrix.LocRotScale(where, rotation3.to_quaternion(), Vector((1, 1, 1)))
    bpy.context.view_layer.update()


def frame_from(x_axis, y_axis):
    y = y_axis.normalized()
    x = (x_axis - y * x_axis.dot(y)).normalized()
    z = x.cross(y)
    return Matrix((x, y, z)).transposed()


def hinge(upper_dir, lower_dir, fallback, sign):
    cross = upper_dir.cross(lower_dir) * sign
    weight = min(1.0, max(0.0, (math.degrees(upper_dir.angle(lower_dir, 0.0)) - 5.0) / 15.0))
    fallback = fallback - upper_dir * fallback.dot(upper_dir)
    if cross.length < 1e-6:
        return fallback.normalized()
    return (cross.normalized() * weight + fallback.normalized() * (1.0 - weight)).normalized()


# Bend-plane continuity per limb: when the arm is nearly straight the pole crosses the
# shoulder-wrist line and the elbow would jump to the other side in one frame; the plane
# turns at most 12 degrees per frame instead (a short swing, not a pop).
BEND_PLANE = {}
MAX_PLANE_STEP = math.radians(12.0)
# A large requested turn goes through the natural side (elbow back/down), never through its
# opposite: there the upper arm rolls past 180 degrees from its rest relation and Rigify's arm
# twist bones flip in one frame (KM466 frames 2688-2716). Frames where this changes the turn
# direction are reported by the collect phase.
PLANE_DETOURS = []
CURRENT_INDEX = [0]


# Wrist roll continuity: a VaM hand controller can spin a full turn around the forearm
# (mocap artifact, KM466 frames 2690-2710). A hand roll passing 180 degrees flips Rigify's
# forearm twist in one frame; the roll returns through 0 instead, at most 20 degrees per frame.
# Frames without such a crossing keep the controller rotation exactly.
WRIST_ROLL = {}
MAX_ROLL_STEP = 20.0
ROLL_FRAMES = []


def continuous_hand(side, index, hand):
    fore = PB["forearm_fk." + side].matrix.to_3x3().normalized()
    rest_rel = REST["forearm_fk." + side].inverted() @ REST["hand_fk." + side]
    q = (rest_rel.inverted() @ fore.inverted() @ hand.normalized()).to_quaternion()
    raw = (math.degrees(2.0 * math.atan2(q.y, q.w)) + 180.0) % 360.0 - 180.0
    out, active = raw, False
    state = WRIST_ROLL.get(side)
    if state is not None and state[0] == index - 1:
        previous, active = state[1], state[2]
        step = (raw - previous + 180.0) % 360.0 - 180.0
        if abs(previous + step) > 180.0:
            active = True
            step -= math.copysign(360.0, step)
        if active:
            out = previous + max(-MAX_ROLL_STEP, min(MAX_ROLL_STEP, step))
            active = abs(step) > MAX_ROLL_STEP
    WRIST_ROLL[side] = (index, out, active)
    if abs(out - raw) < 1e-6:
        return hand
    ROLL_FRAMES.append((side, index))
    swing = q @ Quaternion((0.0, 1.0, 0.0), math.radians(raw)).inverted()
    return fore @ rest_rel @ (swing @ Quaternion((0.0, 1.0, 0.0), math.radians(out))).to_matrix()


def solve_two_bone(upper, lower, start, target, pole, side_key, reference=None):
    length_a, length_b = PB[upper].bone.length, PB[lower].bone.length
    relative = target - start
    distance = min(length_a + length_b - 1e-4, max(abs(length_a - length_b) + 1e-4, relative.length))
    forward = relative.normalized()
    plane = pole - start
    plane = plane - forward * plane.dot(forward)
    if plane.length < 1e-6:
        plane = REST[upper].col[2].copy()
        plane = plane - forward * plane.dot(forward)
    plane.normalize()
    previous = BEND_PLANE.get(side_key)
    if previous is not None:
        previous = previous - forward * previous.dot(forward)
        if previous.length > 1e-6:
            previous.normalize()
            angle = previous.angle(plane, 0.0)
            if angle > MAX_PLANE_STEP:
                direction = 1.0 if previous.cross(plane).dot(forward) >= 0 else -1.0
                natural = None if reference is None else reference - start
                if natural is not None:
                    natural = natural - forward * natural.dot(forward)
                if natural is not None and natural.length > 1e-6:
                    natural.normalize()
                    a = math.atan2(natural.cross(previous).dot(forward), natural.dot(previous))
                    b = math.atan2(natural.cross(plane).dot(forward), natural.dot(plane))
                    if (1.0 if b > a else -1.0) != direction:
                        direction = -direction
                        PLANE_DETOURS.append((side_key, CURRENT_INDEX[0]))
                plane = (Quaternion(forward, MAX_PLANE_STEP * direction) @ previous).normalized()
    BEND_PLANE[side_key] = plane.copy()
    along = (length_a * length_a - length_b * length_b + distance * distance) / (2 * distance)
    middle = start + forward * along + plane * math.sqrt(max(0.0, length_a * length_a - along * along))
    end = start + forward * distance
    return middle, end, max(0.0, relative.length - (length_a + length_b))


def pose_chain(upper, lower, start, middle, end, sign, fallback=None):
    upper_dir = (middle - start).normalized()
    lower_dir = (end - middle).normalized()
    if fallback is None:
        fallback = PB[upper].matrix.to_3x3().col[0].copy()
    axis = hinge(upper_dir, lower_dir, fallback, sign)
    set_world(upper, frame_from(axis, upper_dir), start)
    set_world(lower, frame_from(axis, lower_dir), PB[lower].matrix.translation.copy())


# KittyMocap face layer (ARKit-like, small amplitudes) -> Gratia's anime shapes. Gains lift the
# subtle source values to readable anime expressions. Channels with a constant rest bias (Nose
# Sneer, Mouth Upper Up / Lower Down, Mouth Shrug), jaw/mouth sideways, dimples and rolls have no
# Gratia counterpart and are not transferred.
K = "geometry/KittyMocap "
FACE_MAP = {
    "Eye L close": [("geometry/Eyes Closed Left", 1.0)],
    "Eye R close": [("geometry/Eyes Closed Right", 1.0)],
    "Mouth smile": [(K + "Mouth Smile Left", 0.75), (K + "Mouth Smile Right", 0.75)],
    "Mouth L up": [(K + "Mouth Smile Left", 1.5), (K + "Mouth Smile Right", -1.5)],
    "Mouth R up": [(K + "Mouth Smile Right", 1.5), (K + "Mouth Smile Left", -1.5)],
    "Mouth open 1": [(K + "Jaw Open", 2.5)],
    "Brows worry 1": [(K + "Brow Inner Up", 1.5)],
    "Brows angry 1": [(K + "Brow Down Left", 1.0), (K + "Brow Down Right", 1.0)],
    "Brows up": [(K + "Brow Outer Up Left", 1.5), (K + "Brow Outer Up Right", 1.5)],
    "Eyes smug": [(K + "Eye Squint Left", 0.5), (K + "Eye Squint Right", 0.5)],
    "Eyes surprised": [(K + "Eye Wide Left", 2.0), (K + "Eye Wide Right", 2.0)],
    "Mouth kiss": [(K + "Mouth Pucker", 1.5)],
    "Mouth o": [(K + "Mouth Funnel", 2.0)],
    "Mouth sad 1": [(K + "Mouth Frown Left Copy", 1.0), (K + "Mouth Frown Right Copy", 1.0)],
    "Mouth puff out": [(K + "Cheek Puff", 3.0)],
    "Mouth widen 1": [(K + "Mouth Stretch Left", 2.0), (K + "Mouth Stretch Right", 2.0)],
    "Mouth shrink": [(K + "Mouth Press Left", 1.0), (K + "Mouth Press Right", 1.0)],
    "Brows worry 2": [("geometry/morph: AAsex_sqntwrry1sm1", 0.6)],
}


def face_values(index):
    floats = frames[index]["float_parameters"]
    return {key: min(1.0, max(0.0, sum(floats.get(name, 0.0) * w for name, w in parts))) for key, parts in FACE_MAP.items()}


def pose_at(t, lift):
    """Solve Gratia's controls for source time t; lift is the global floor offset (metres)."""
    index, s = sample_at(t)
    CURRENT_INDEX[0] = index
    reset_pose()
    rotation = {k: v[1] for k, v in s.items()}
    position = {k: v[0] * SCALE + Vector((0.0, 0.0, lift)) for k, v in s.items()}
    hips_target = (position["hipJoint.L"] + position["hipJoint.R"]) * 0.5
    # Pelvis: torso control carries the hip rotation; hips control stays at rest.
    torso_rot = rotation["hipControl"] @ REST["torso"]
    pivot = hips_target - rotation["hipControl"] @ (HIPS_REST - TORSO_PIVOT_REST)
    set_world("torso", torso_rot, pivot)
    set_world("chest", rotation["chestControl"] @ REST["chest"])
    neck_q = rotation["chestControl"].to_quaternion().slerp(rotation["headControl"].to_quaternion(), 0.5)
    set_world("neck", neck_q.to_matrix() @ REST["neck"])
    set_world("head", rotation["headControl"] @ REST["head"])
    chest_ref = PB["DEF-spine.003"].matrix @ CHEST_REF_LOCAL
    solves = {}
    for side, prefix, out_sign in (("L", "l", 1.0), ("R", "r", -1.0)):
        upper, lower = "upper_arm_fk." + side, "forearm_fk." + side
        shoulder = PB[upper].matrix.translation.copy()
        chest_point = s["chestControl"][0]
        wrist = chest_ref + (s[prefix + "HandControl"][0] - chest_point) * SCALE
        elbow = chest_ref + (s[prefix + "ElbowControl"][0] - chest_point) * SCALE
        chest_rot = rotation["chestControl"]
        natural = shoulder + chest_rot @ Vector((out_sign * 0.10, 0.30, -0.20))
        w = elbow_weight[side][index]
        pole = elbow * w + natural * (1.0 - w)
        middle, end, short = solve_two_bone(upper, lower, shoulder, wrist, pole, side, natural)
        # Nearly straight, the elbow hinge tends to plane x forward (the limit of upper x lower):
        # a continuous fallback, so the upper arm never twists when the elbow straightens.
        forward = (wrist - shoulder).normalized()
        plane_hinge = BEND_PLANE[side].cross(forward) * HINGE[(upper, lower)]
        pose_chain(upper, lower, shoulder, middle, end, HINGE[(upper, lower)], plane_hinge)
        set_world("hand_fk." + side, continuous_hand(side, index, rotation[prefix + "HandControl"] @ HAND_T[side]))
        solves["arm." + side] = {"unreachable_m": short, "elbow_weight": w}
        # Leg: aim along the source segments; the knee hinge lies in the bend plane.
        thigh, shin = "thigh_fk." + side, "shin_fk." + side
        hip = PB[thigh].matrix.translation.copy()
        thigh_dir = (s[prefix + "KneeControl"][0] - s["hipJoint." + side][0]).normalized()
        shin_dir = (s[prefix + "FootControl"][0] - s[prefix + "KneeControl"][0]).normalized()
        knee = hip + thigh_dir * PB[thigh].bone.length
        ankle = knee + shin_dir * PB[shin].bone.length
        pose_chain(thigh, shin, hip, knee, ankle, HINGE[(thigh, shin)])
        set_world("foot_fk." + side, rotation[prefix + "FootControl"] @ REST["foot_fk." + side])
        solves["leg." + side] = {"ankle_vs_source_m": (PB["foot_fk." + side].matrix.translation - position[prefix + "FootControl"]).length,
                                 "knee_bend_deg": math.degrees(thigh_dir.angle(shin_dir, 0.0))}
        # Fingers: bend about each bone's X axis (positive curls toward the palm; checked below).
        floats = frames[index]["float_parameters"]
        hand_key = "LeftHandFingerControl" if side == "L" else "RightHandFingerControl"
        for finger in FINGERS:
            for bone_name, joint in zip(finger_bones(finger, side), ("Proximal", "Middle", "Distal")):
                value = floats.get("%s/%s%sBend" % (hand_key, finger, joint))
                if value is None or bone_name not in PB:
                    continue
                # Target flexion expressed about +X, minus the bend Gratia already has at rest.
                delta = FINGER_SIGN * value - FINGER_REST[bone_name]
                PB[bone_name].rotation_quaternion = Quaternion(Vector((1, 0, 0)), math.radians(delta))
    bpy.context.view_layer.update()
    return index, s, solves


def finger_sign():
    """+X rotation must bring the fingertip toward the palm (closer to the wrist than -X)."""
    reset_pose()
    bone = PB["f_middle.02.L"]
    wrist = PB["hand_fk.L"].matrix.translation.copy()
    result = {}
    for sign in (1.0, -1.0):
        bone.rotation_quaternion = Quaternion(Vector((1, 0, 0)), math.radians(25 * sign))
        bpy.context.view_layer.update()
        result[sign] = (PB["f_middle.03.L"].matrix @ Vector((0, PB["f_middle.03.L"].bone.length, 0)) - wrist).length
    reset_pose()
    return 1.0 if result[1.0] < result[-1.0] else -1.0


FINGER_SIGN = finger_sign()


def body_meshes():
    names = ("Body", "Boots", "Pants", "Top", "Gloves")
    return [o for o in bpy.data.objects if o.type == "MESH" and o.name in names and o.find_armature() == source]


def lowest_point(depsgraph):
    low = math.inf
    for obj in body_meshes():
        evaluated = obj.evaluated_get(depsgraph)
        mesh = evaluated.to_mesh()
        coords = np.empty(len(mesh.vertices) * 3, dtype=np.float32)
        mesh.vertices.foreach_get("co", coords)
        world = np.array(evaluated.matrix_world, dtype=np.float64)
        evaluated.to_mesh_clear()
        z = coords.reshape((-1, 3)).astype(np.float64) @ world[2, :3] + world[2, 3]
        low = min(low, float(z.min()))
    return low


def markers(s, lift):
    """Red spheres at the scaled source joints (preview only); returns created objects."""
    created = []
    material = bpy.data.materials.new("ClaudeMarker")
    material.diffuse_color = (1.0, 0.05, 0.05, 1.0)
    names = ["hipJoint.L", "hipJoint.R", "lKneeControl", "rKneeControl", "lFootControl", "rFootControl",
             "chestControl", "headControl", "lElbowControl", "rElbowControl", "lHandControl", "rHandControl"]
    for name in names:
        mesh = bpy.data.meshes.new("ClaudeMarker_" + name)
        obj = bpy.data.objects.new("ClaudeMarker_" + name, mesh)
        scene.collection.objects.link(obj)
        import bmesh
        bm = bmesh.new()
        bmesh.ops.create_uvsphere(bm, u_segments=12, v_segments=8, radius=0.025)
        bm.to_mesh(mesh)
        bm.free()
        mesh.materials.append(material)
        obj.color = (1.0, 0.05, 0.05, 1.0)
        obj.location = s[name][0] * SCALE + Vector((0.0, 0.0, lift))
        obj.show_in_front = True
        created.append(obj)
    return created, material


def render_views(prefix, views, xray=False):
    saved = {"camera": scene.camera, "engine": scene.render.engine, "path": scene.render.filepath,
             "res": (scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage)}
    shading = scene.display.shading
    saved_shading = (shading.light, shading.color_type, shading.show_xray, shading.xray_alpha)
    hide = {o.name: o.hide_render for o in scene.objects}
    camera_data = bpy.data.cameras.new("ClaudePreviewCam")
    camera = bpy.data.objects.new("ClaudePreviewCam", camera_data)
    scene.collection.objects.link(camera)
    files = []
    surface_deform = []
    try:
        # Only the meshes the game exports (no physics cages/collision proxies, no game-rig copies).
        # Surface Deform from the Blender cloth cages is render-only and needs a sequential cloth
        # simulation; the game has no cages (KawaiiPhysics instead), so renders show the armature pose.
        visible = {original.name for original, _ in pairs}
        for obj in scene.objects:
            if obj.type == "MESH" and not obj.name.startswith("ClaudeMarker"):
                obj.hide_render = obj.name not in visible
                for modifier in obj.modifiers:
                    if modifier.type == "SURFACE_DEFORM" and modifier.show_render:
                        surface_deform.append(modifier)
                        modifier.show_render = False
        scene.camera = camera
        scene.render.engine = "BLENDER_WORKBENCH"
        shading.light, shading.color_type = "STUDIO", "MATERIAL"
        shading.show_xray, shading.xray_alpha = xray, 0.35
        scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage = 520, 680, 100
        for name, location, rotation, lens in views:
            camera.location = Vector(location)
            camera.rotation_euler = Euler([math.radians(a) for a in rotation])
            camera_data.lens = lens
            scene.render.filepath = str(EVIDENCE / f"{prefix}_{name}{'_xray' if xray else ''}.png")
            bpy.ops.render.render(write_still=True)
            files.append(scene.render.filepath)
    finally:
        for modifier in surface_deform:
            modifier.show_render = True
        for obj_name, value in hide.items():
            if obj_name in scene.objects:
                scene.objects[obj_name].hide_render = value
        scene.camera = saved["camera"]
        scene.render.engine = saved["engine"]
        scene.render.filepath = saved["path"]
        scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage = saved["res"]
        shading.light, shading.color_type, shading.show_xray, shading.xray_alpha = saved_shading
        bpy.data.objects.remove(camera)
        bpy.data.cameras.remove(camera_data)
    return files


# ---------------------------------------------------------------- authoring
END = int(round(DURATION * FPS)) + 1
PREFIX = CLIP + "_"
LIFT_PATH = EVIDENCE / ("%s_floor.json" % STEM)
SOURCE_KEYS = (["torso", "hips", "chest", "neck", "head"]
               + [n + "." + s for s in ("L", "R") for n in ("upper_arm_fk", "forearm_fk", "hand_fk", "thigh_fk", "shin_fk",
                                                           "foot_fk", "toe_fk", "shoulder")]
               + [b for s in ("L", "R") for f in FINGERS for b in finger_bones(f, s)]
               + ["thumb.0%d.%s" % (k, s) for s in ("L", "R") for k in (1, 2, 3)])
SOURCE_KEYS = [name for name in SOURCE_KEYS if name in PB]


def depth(bone):
    return 0 if not bone.parent else depth(bone.parent) + 1


ordered = sorted(game.pose.bones, key=lambda b: depth(b.bone))


def curves(action):
    return [curve for layer in action.layers for strip in layer.strips for bag in strip.channelbags for curve in bag.fcurves]


def linear(action):
    """Sign-continuous quaternion keys (chunk boundaries included) with linear interpolation."""
    groups = {}
    for curve in curves(action):
        if curve.data_path.endswith("rotation_quaternion"):
            groups.setdefault(curve.data_path, {})[curve.array_index] = curve
    for path, parts in groups.items():
        if len(parts) != 4:
            continue
        points = [parts[i].keyframe_points for i in range(4)]
        count = len(points[0])
        assert all(len(p) == count for p in points), path
        previous = None
        for k in range(count):
            q = [points[i][k].co[1] for i in range(4)]
            if previous is not None and sum(a * b for a, b in zip(q, previous)) < 0:
                for i in range(4):
                    points[i][k].co[1] = -q[i]
                q = [-v for v in q]
            previous = q
    for curve in curves(action):
        for key in curve.keyframe_points:
            key.interpolation = "LINEAR"
        curve.update()


def shape_objects():
    return [obj for original, candidate in pairs for obj in (original, candidate) if obj.data.shape_keys]


def create_actions():
    source.animation_data_create()
    game.animation_data_create()
    names = {"source": PREFIX + "Source", "game": PREFIX + "Game", "shapes": {}}
    for name in (names["source"], names["game"]):
        bpy.data.actions.new(name).use_fake_user = True
    for obj in shape_objects():
        obj.data.shape_keys.animation_data_create()
        action = bpy.data.actions.new(PREFIX + "Shapes_" + obj.name)
        action.use_fake_user = True
        names["shapes"][obj.name] = action.name
    (EVIDENCE / ("%s_actions.json" % STEM)).write_text(json.dumps(names, indent=2), encoding="utf-8")


def action_names():
    return json.loads((EVIDENCE / ("%s_actions.json" % STEM)).read_text(encoding="utf-8"))


def assign(names):
    source.animation_data_create()
    game.animation_data_create()
    source.animation_data.action = bpy.data.actions[names["source"]]
    game.animation_data.action = bpy.data.actions[names["game"]]
    for obj in shape_objects():
        obj.data.shape_keys.animation_data_create()
        obj.data.shape_keys.animation_data.action = bpy.data.actions[names["shapes"][obj.name]]


def hide_meshes():
    """Pose solving only needs the armatures; hidden meshes are not evaluated."""
    hidden = []
    for obj in bpy.data.objects:
        if obj.type == "MESH" and not obj.hide_viewport:
            obj.hide_viewport = True
            hidden.append(obj)
    return hidden


def key_source(frame):
    for name in SOURCE_KEYS:
        bone = PB[name]
        if name == "torso":
            bone.keyframe_insert("location", frame=frame, group=name)
        channel = {"QUATERNION": "rotation_quaternion", "AXIS_ANGLE": "rotation_axis_angle"}.get(bone.rotation_mode, "rotation_euler")
        bone.keyframe_insert(channel, frame=frame, group=name)


def key_shapes(frame, index, constant):
    face = face_values(index)
    for obj in shape_objects():
        for key in obj.data.shape_keys.key_blocks[1:]:
            if key.name in face:
                key.value = face[key.name]
            elif constant:
                key.value = 1.0 if key.name == "Game_NeutralCorrective" else 0.0
            else:
                continue
            key.keyframe_insert("value", frame=frame)


bake_last = {}


def bake_game(frame):
    depsgraph = frame_set(frame)
    evaluated = source.evaluated_get(depsgraph)
    transform = game.matrix_world.inverted() @ source.matrix_world
    targets = {bone.name: transform @ evaluated.pose.bones[bone.name].matrix.copy() for bone in ordered}
    for bone in ordered:
        kwargs = {"parent_matrix": targets[bone.parent.name], "parent_matrix_local": bone.parent.bone.matrix_local} if bone.parent else {}
        basis = bone.bone.convert_local_to_pose(targets[bone.name], bone.bone.matrix_local, invert=True, **kwargs)
        position, rotation, scale = basis.decompose()
        if bone.name in bake_last and rotation.dot(bake_last[bone.name]) < 0:
            rotation.negate()
        bake_last[bone.name] = rotation.copy()
        bone.location, bone.rotation_quaternion, bone.scale = position, rotation, scale
        for channel in ("location", "rotation_quaternion", "scale"):
            bone.keyframe_insert(channel, frame=frame, group=bone.name)


def positions(obj, depsgraph):
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    coords = np.empty(len(mesh.vertices) * 3, dtype=np.float32)
    mesh.vertices.foreach_get("co", coords)
    world = np.array(evaluated.matrix_world, dtype=np.float64)
    evaluated.to_mesh_clear()
    return coords.reshape((-1, 3)).astype(np.float64) @ world[:3, :3].T + world[:3, 3]


def validate_range(names, start, stop):
    """validate() over [start, stop] for one long-take segment (actions already assigned)."""
    assign(names)
    global END_VALIDATE
    END_VALIDATE = (start, stop)
    try:
        return validate(names)
    finally:
        END_VALIDATE = None


END_VALIDATE = None


def export_range(names, start, stop, filename):
    assign(names)
    scene.render.fps, scene.render.fps_base = FPS, 1
    scene.frame_start, scene.frame_end = start, stop
    frame_set(start)
    selected = [game] + [candidate for _, candidate in pairs]
    for obj in scene.objects:
        obj.select_set(False)
    for obj in selected:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = game
    path = OUT / filename
    with bpy.context.temp_override(active_object=game, object=game, selected_objects=selected, selected_editable_objects=selected):
        assert "FINISHED" in bpy.ops.export_scene.fbx(
            filepath=str(path), use_selection=True, object_types={"MESH", "ARMATURE"}, global_scale=1,
            apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS", axis_forward="-Y", axis_up="Z",
            use_mesh_modifiers=False, mesh_smooth_type="FACE", add_leaf_bones=False,
            use_armature_deform_only=True, armature_nodetype="NULL", path_mode="AUTO", embed_textures=False,
            bake_anim=True, bake_anim_use_all_bones=True, bake_anim_use_nla_strips=False,
            bake_anim_use_all_actions=False, bake_anim_force_startend_keying=True,
            bake_anim_step=1, bake_anim_simplify_factor=0)
    with path.open("rb") as stream:
        return {"exported": True, "fbx": str(path), "bytes": path.stat().st_size, "sha256": hashlib.file_digest(stream, "sha256").hexdigest(),
                "frames": [start, stop]}


def validate(names):
    """Whole and half frames: game rig + LBS candidates against the source evaluated meshes.

    The source keeps DQS (preserve volume) and B-Bones; Blender cloth cages (Surface Deform,
    render only) are excluded, as in the game rig preparation."""
    scene.render.fps, scene.render.fps_base = FPS, 1
    reset_pose()
    max_errors = {original.name: 0.0 for original, _ in pairs}
    between_errors = {original.name: 0.0 for original, _ in pairs}
    worst_frames = {original.name: None for original, _ in pairs}
    worst_bones, max_angle, samples = {}, 0.0, 0
    contact = {"source_min_z": math.inf, "game_min_z": math.inf}
    first, last_frame = END_VALIDATE or (1, END)
    for frame in np.arange(first, last_frame + 0.1, 0.5):
        depsgraph = frame_set(float(frame))
        eval_source = source.evaluated_get(depsgraph)
        eval_game = game.evaluated_get(depsgraph)
        keyed = float(frame).is_integer()
        if keyed:
            for bone in ordered:
                angle = eval_source.pose.bones[bone.name].matrix.to_quaternion().rotation_difference(
                    eval_game.pose.bones[bone.name].matrix.to_quaternion()).angle
                angle = math.degrees(min(angle, abs(2 * math.pi - angle)))
                if angle > worst_bones.get(bone.name, (-1, 0))[0]:
                    worst_bones[bone.name] = (angle, float(frame))
                max_angle = max(max_angle, angle)
        for original, candidate in pairs:
            a, b = positions(original, depsgraph), positions(candidate, depsgraph)
            errors = np.linalg.norm(a - b, axis=1)
            assert np.isfinite(errors).all(), original.name
            target = max_errors if keyed else between_errors
            if float(errors.max(initial=0)) > target[original.name]:
                target[original.name] = float(errors.max(initial=0))
                if keyed:
                    worst_frames[original.name] = float(frame)
            if original.name in ("Body", "Boots"):
                contact["source_min_z"] = min(contact["source_min_z"], float(a[:, 2].min()))
                contact["game_min_z"] = min(contact["game_min_z"], float(b[:, 2].min()))
        samples += 1
    tolerance = {"vertex_m": 0.002, "between_keys_vertex_m": 0.005, "angle_deg": 0.25}
    worst = max(max_errors.values())
    return {
        "clip": CLIP, "duration_seconds": DURATION, "fps": FPS, "frames": END, "samples": samples,
        "validated_frames": [first, last_frame],
        "source_action": names["source"], "game_action": names["game"], "shape_actions": names["shapes"],
        "max_vertex_error_metres": max_errors, "max_between_keys_vertex_error_metres": between_errors,
        "worst_vertex_frames": worst_frames, "max_bone_angle_error_degrees": max_angle,
        "worst_angle_bones": sorted(((k, v[0], v[1]) for k, v in worst_bones.items()), key=lambda kv: -kv[1])[:8],
        "floor_contact": contact, "tolerance": tolerance,
        "excluded": "Blender cloth cages (Surface Deform from TitsPhys/ThighsPhys/AssPhys) are render-only and not exported",
        "passed": worst <= tolerance["vertex_m"] and max(between_errors.values()) <= tolerance["between_keys_vertex_m"]
                  and max_angle <= tolerance["angle_deg"],
        "exported": False}


# ---------------------------------------------------------------- long take
CHUNKS = EVIDENCE / "chunks"
BASIS_PATH = EVIDENCE / ("%s_basis.npz" % STEM)
TOP_GAME_BONES = [bone.name for bone in ordered if bone.parent is None]


def source_values():
    """Keyed source control channels: torso location and every control rotation (quaternion or Euler)."""
    rotations = []
    for name in SOURCE_KEYS:
        bone = PB[name]
        if bone.rotation_mode == "QUATERNION":
            rotations.append(list(bone.rotation_quaternion))
        else:
            rotations.append(list(bone.rotation_euler) + [0.0])
    return list(PB["torso"].location), rotations


def game_values(depsgraph, last):
    evaluated = source.evaluated_get(depsgraph)
    transform = game.matrix_world.inverted() @ source.matrix_world
    targets = {bone.name: transform @ evaluated.pose.bones[bone.name].matrix.copy() for bone in ordered}
    rows = []
    for bone in ordered:
        kwargs = {"parent_matrix": targets[bone.parent.name], "parent_matrix_local": bone.parent.bone.matrix_local} if bone.parent else {}
        basis = bone.bone.convert_local_to_pose(targets[bone.name], bone.bone.matrix_local, invert=True, **kwargs)
        position, rotation, scale = basis.decompose()
        if bone.name in last and rotation.dot(last[bone.name]) < 0:
            rotation.negate()
        last[bone.name] = rotation.copy()
        rows.append(list(position) + list(rotation) + list(scale))
    return rows


def segments():
    """Inclusive frame ranges; consecutive segments share their boundary frame (continuous playback)."""
    out, start = [], 1
    while start < END:
        stop = min(END, start + SEGMENT_FRAMES)
        out.append((start, stop))
        start = stop
    return out


def load_collected():
    frames, src_loc, src_rot, game_rows, low_frames, low_values = [], [], [], [], [], []
    for path in sorted(CHUNKS.glob("collect_*.npz")):
        data_chunk = np.load(path)
        frames.append(data_chunk["frames"]); src_loc.append(data_chunk["src_loc"]); src_rot.append(data_chunk["src_rot"])
        game_rows.append(data_chunk["game"]); low_frames.append(data_chunk["low_frames"]); low_values.append(data_chunk["low_values"])
    frames = np.concatenate(frames)
    order = np.argsort(frames)
    assert np.array_equal(frames[order], np.arange(1, END + 1)), "collect is incomplete or overlapping"
    lf = np.concatenate(low_frames); lv = np.concatenate(low_values)
    lo = np.argsort(lf)
    return (np.concatenate(src_loc)[order], np.concatenate(src_rot)[order], np.concatenate(game_rows)[order], lf[lo], lv[lo])


def lift_curve(low_frames, low_values):
    """Per-frame floor offset: the lowest body point touches the floor, lightly smoothed (0.3 s)."""
    lows = np.interp(np.arange(1, END + 1), low_frames, low_values)
    width = 9
    padded = np.pad(lows, width // 2, mode="edge")
    smooth = np.convolve(padded, np.ones(width) / width, mode="valid")
    return -smooth


def apply_frame(src_loc, src_rot, game_row, lift):
    """Set one collected frame directly on both rigs (no actions)."""
    PB["torso"].location = Vector(src_loc) + Vector((0.0, 0.0, lift))
    for name, value in zip(SOURCE_KEYS, src_rot):
        bone = PB[name]
        if bone.rotation_mode == "QUATERNION":
            bone.rotation_quaternion = Quaternion(value)
        else:
            bone.rotation_euler = Euler(value[:3], bone.rotation_mode)
    for bone, row in zip(ordered, game_row):
        location = Vector(row[0:3])
        if bone.name in TOP_GAME_BONES:
            location = location + bone.bone.matrix_local.to_3x3().inverted() @ Vector((0.0, 0.0, lift))
        bone.location, bone.rotation_quaternion, bone.scale = location, Quaternion(row[3:7]), Vector(row[7:10])
    bpy.context.view_layer.update()
    return bpy.context.evaluated_depsgraph_get()


def bulk_curve(target, action, data_path, index, frames, values, group):
    curve = action.fcurve_ensure_for_datablock(target, data_path, index=index, group_name=group)
    curve.keyframe_points.clear()
    curve.keyframe_points.add(len(frames))
    co = np.empty(len(frames) * 2, dtype=np.float32)
    co[0::2] = frames
    co[1::2] = values
    curve.keyframe_points.foreach_set("co", co)
    curve.keyframe_points.foreach_set("interpolation", np.ones(len(frames), dtype=np.int32))
    curve.update()


def continuous(quaternions):
    """Flip quaternion signs so consecutive keys stay on one hemisphere (axis 0 = time)."""
    q = quaternions.copy()
    for i in range(1, len(q)):
        if np.dot(q[i], q[i - 1]) < 0:
            q[i] = -q[i]
    return q


def rest_delta_factory():
    weighted = {}
    for _, candidate in pairs:
        groups = {g.index: g.name for g in candidate.vertex_groups}
        vi, bone_names, ws = [], [], []
        for vertex in candidate.data.vertices:
            for element in vertex.groups:
                name = groups[element.group]
                if name in game.data.bones and element.weight > 1e-8:
                    vi.append(vertex.index); bone_names.append(name); ws.append(element.weight)
        weighted[candidate.name] = (np.array(vi), bone_names, np.array(ws))
    to_armature = np.array(game.matrix_world.inverted(), dtype=np.float64)[:3, :3]

    def rest_delta(candidate, difference_world, depsgraph, idx):
        rig = game.evaluated_get(depsgraph)
        vi, bone_names, ws = weighted[candidate.name]
        count = len(candidate.data.vertices)
        skin = {n: np.array(rig.pose.bones[n].matrix @ game.data.bones[n].matrix_local.inverted(), dtype=np.float64)[:3, :3]
                for n in set(bone_names)}
        matrices = np.zeros((count, 3, 3)); total = np.zeros(count)
        np.add.at(matrices, vi, np.array([skin[n] for n in bone_names]) * ws[:, None, None])
        np.add.at(total, vi, ws)
        mask = total > 1e-8
        matrices[mask] /= total[mask, None, None]
        matrices[~mask] = np.eye(3)
        return np.linalg.solve(matrices[idx], (difference_world[idx] @ to_armature.T)[..., None])[..., 0]
    return rest_delta


VIEWS = [("front", (0, -3.0, 0.75), (85, 0, 0), 45), ("side", (3.0, 0.0, 0.75), (85, 0, 90), 45),
         ("three_quarter", (1.9, -2.4, 1.1), (80, 0, 38), 45)]
LEG_PROPS = [("thigh_parent.L", "IK_FK"), ("thigh_parent.R", "IK_FK")]
saved_props = {bone: PB[bone][prop] for bone, prop in LEG_PROPS}
saved_actions = {"source": source.animation_data.action if source.animation_data else None,
                 "game": game.animation_data.action if game.animation_data else None}
saved_frame = scene.frame_current
saved_basis = {bone.name: (bone.location.copy(), bone.rotation_quaternion.copy(), tuple(bone.rotation_euler),
                           tuple(bone.rotation_axis_angle), bone.scale.copy()) for bone in PB}
calibration = {
    "scale": SCALE, "yaw_degrees": math.degrees(yaw), "center_m": list(CENTER), "finger_sign": FINGER_SIGN,
    "hinge_signs": {"%s/%s" % k: v for k, v in HINGE.items()},
    "hand_t_pose_check": {side: list(HAND_T[side].col[1]) for side in ("L", "R")},
    "elbow_weight_min": {k: min(v) for k, v in elbow_weight.items()},
}


def restore():
    if source.animation_data:
        source.animation_data.action = saved_actions["source"]
    if game.animation_data:
        game.animation_data.action = saved_actions["game"]
    for bone, prop in LEG_PROPS:
        PB[bone][prop] = saved_props[bone]
    # Shape keys end neutral (no clip action assigned; candidates keep their base corrective).
    for original, candidate in pairs:
        for obj in (original, candidate):
            keys = obj.data.shape_keys
            if not keys:
                continue
            if keys.animation_data:
                keys.animation_data.action = None
            for key in keys.key_blocks[1:]:
                key.value = 1.0 if key.name == "Game_NeutralCorrective" else 0.0
    # Unkeyed bones keep the pose stored in the file.
    for bone in PB:
        location, quaternion, euler, axis_angle, scale = saved_basis[bone.name]
        bone.location, bone.rotation_quaternion, bone.scale = location, quaternion, scale
        bone.rotation_euler, bone.rotation_axis_angle = euler, axis_angle
    frame_set(saved_frame)


# Legs are posed with FK for this clip; restored afterwards (other clips use leg IK).
for bone, prop in LEG_PROPS:
    PB[bone][prop] = 1.0
if source.animation_data:
    source.animation_data.action = None
if game.animation_data:
    game.animation_data.action = None

try:
    if PHASE == "preview":
        lows = {}
        for t in PREVIEW_TIMES:
            pose_at(t, 0.0)
            lows[t] = lowest_point(bpy.context.evaluated_depsgraph_get())
        lift = -min(lows.values())
        outputs = {}
        for t in PREVIEW_TIMES:
            index, s, solves = pose_at(t, lift)
            created, material = markers(s, lift)
            try:
                renders = render_views("preview_%04.1fs" % t, VIEWS) + render_views("preview_%04.1fs" % t, VIEWS[:2], xray=True)
                outputs["%.1f" % t] = {"renders": renders, "solves": solves,
                                       "lowest_m": lowest_point(bpy.context.evaluated_depsgraph_get())}
            finally:
                for obj in created:
                    mesh = obj.data
                    bpy.data.objects.remove(obj)
                    bpy.data.meshes.remove(mesh)
                bpy.data.materials.remove(material)
        result = {"phase": PHASE, "lift_m": lift, "calibration": calibration, "outputs": outputs}
    elif PHASE == "lift":
        # Lowest point of the clip touches the floor (sampled every 5th frame, armature pose).
        lows = {}
        measured = body_meshes()
        hidden = hide_meshes()
        try:
            for frame in list(range(1, END + 1, 5)) + [END]:
                pose_at((frame - 1) / FPS, 0.0)
                for obj in measured:
                    obj.hide_viewport = False
                lows[frame] = lowest_point(bpy.context.evaluated_depsgraph_get())
                for obj in measured:
                    obj.hide_viewport = True
        finally:
            for obj in hidden:
                obj.hide_viewport = False
        lowest = min(lows, key=lows.get)
        lift = -lows[lowest]
        LIFT_PATH.write_text(json.dumps({"lift_m": lift, "lowest_frame": lowest, "lows": lows, "calibration": calibration}, indent=2),
                             encoding="utf-8")
        result = {"phase": PHASE, "lift_m": lift, "lowest_frame": lowest,
                  "highest_low_m": max(lows.values()) + lift}
    elif PHASE == "author":
        lift = json.loads(LIFT_PATH.read_text(encoding="utf-8"))["lift_m"]
        start, stop = globals().get("RANGE", (1, END))
        if start == 1:
            for action in list(bpy.data.actions):
                if action.name.startswith(PREFIX):
                    bpy.data.actions.remove(action)
            create_actions()
        names = action_names()
        assign(names)
        hidden = hide_meshes()
        try:
            for frame in range(start, stop + 1):
                index, _, solves = pose_at((frame - 1) / FPS, lift)
                key_source(frame)
                key_shapes(frame, index, constant=frame in (1, END))
                bake_game(frame)
        finally:
            for obj in hidden:
                obj.hide_viewport = False
        result = {"phase": PHASE, "range": [start, stop], "lift_m": lift}
    elif PHASE == "validate":
        names = action_names()
        for name in [names["source"], names["game"]] + list(names["shapes"].values()):
            linear(bpy.data.actions[name])
        assign(names)
        report = validate(names)
        report["calibration"] = json.loads(LIFT_PATH.read_text(encoding="utf-8"))["calibration"]
        report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
        result = {"phase": PHASE, "passed": report["passed"],
                  "worst_mm": max(report["max_vertex_error_metres"].values()) * 1000,
                  "between_mm": max(report["max_between_keys_vertex_error_metres"].values()) * 1000,
                  "angle_deg": report["max_bone_angle_error_degrees"], "worst_bones": report["worst_angle_bones"],
                  "per_mesh_mm": {k: round(v * 1000, 2) for k, v in report["max_vertex_error_metres"].items()}}
    elif PHASE == "correct":
        # The source skin keeps DQS/B-Bones, the candidates are LBS as in Unreal. Rest-space deltas
        # (inverse weighted LBS) of every frame are compressed by PCA into signed shapes.
        names = action_names()
        corrective_prefix = "Game_" + CLIP + "_"
        max_rank = globals().get("MAX_RANK", 24)
        target_m = 0.0008
        for _, candidate in pairs:
            for key in list(candidate.data.shape_keys.key_blocks):
                if key.name.startswith(corrective_prefix):
                    candidate.shape_key_remove(key)
        for action_name in names["shapes"].values():
            for bag in [b for layer in bpy.data.actions[action_name].layers for strip in layer.strips for b in strip.channelbags]:
                for curve in list(bag.fcurves):
                    if corrective_prefix in curve.data_path:
                        bag.fcurves.remove(curve)
        for name in [names["source"], names["game"]] + list(names["shapes"].values()):
            linear(bpy.data.actions[name])
        assign(names)
        reset_pose()
        weighted = {}
        for _, candidate in pairs:
            relative = np.array(game.matrix_world.inverted() @ candidate.matrix_world)[:3, :3]
            assert np.allclose(relative, np.eye(3), atol=1e-5), candidate.name
            groups = {g.index: g.name for g in candidate.vertex_groups}
            vi, bone_names, ws = [], [], []
            for vertex in candidate.data.vertices:
                for element in vertex.groups:
                    name = groups[element.group]
                    if name in game.data.bones and element.weight > 1e-8:
                        vi.append(vertex.index); bone_names.append(name); ws.append(element.weight)
            weighted[candidate.name] = (np.array(vi), bone_names, np.array(ws))
        to_armature = np.array(game.matrix_world.inverted(), dtype=np.float64)[:3, :3]

        def rest_delta(candidate, difference_world, depsgraph):
            rig = game.evaluated_get(depsgraph)
            vi, bone_names, ws = weighted[candidate.name]
            count = len(candidate.data.vertices)
            skin = {n: np.array(rig.pose.bones[n].matrix @ game.data.bones[n].matrix_local.inverted(), dtype=np.float64)[:3, :3]
                    for n in set(bone_names)}
            matrices = np.zeros((count, 3, 3)); total = np.zeros(count)
            np.add.at(matrices, vi, np.array([skin[n] for n in bone_names]) * ws[:, None, None])
            np.add.at(total, vi, ws)
            mask = total > 1e-8
            matrices[mask] /= total[mask, None, None]
            matrices[~mask] = np.eye(3)
            return np.linalg.solve(matrices, (difference_world @ to_armature.T)[..., None])[..., 0]

        originals = {c.name: o for o, c in pairs}
        active = {c.name: np.zeros(len(c.data.vertices), dtype=bool) for _, c in pairs}
        for frame in range(1, END + 1, 2):
            depsgraph = frame_set(frame)
            for original, candidate in pairs:
                active[candidate.name] |= np.linalg.norm(positions(original, depsgraph) - positions(candidate, depsgraph), axis=1) > 0.0003
        layout = [(candidate, np.nonzero(active[candidate.name])[0]) for _, candidate in pairs if active[candidate.name].any()]
        rows = []
        for frame in range(1, END + 1):
            depsgraph = frame_set(frame)
            parts = []
            for candidate, idx in layout:
                difference = positions(originals[candidate.name], depsgraph) - positions(candidate, depsgraph)
                parts.append(rest_delta(candidate, difference, depsgraph)[idx].astype(np.float32).ravel())
            rows.append(np.concatenate(parts))
        D = np.stack(rows).astype(np.float64)
        values, vectors = np.linalg.eigh(D @ D.T)
        vectors = vectors[:, values.argsort()[::-1]]
        chosen, residual = max_rank, None
        residuals = []
        for rank in range(1, max_rank + 1):
            U = vectors[:, :rank]
            rest = D - U @ (U.T @ D)
            residual = float(np.linalg.norm(rest.reshape(len(rows), -1, 3), axis=2).max())
            residuals.append(residual)
            if residual <= target_m:
                chosen = rank
                break
        U = vectors[:, :chosen]
        basis = D.T @ U
        # Coefficients span +-1 (shape = largest per-frame offset), so Unreal's curve compression
        # error stays small relative to the corrective.
        scales = np.array([max(1e-9, float(np.abs(U[:, k]).max())) for k in range(chosen)])
        basis, coefficients = basis * scales, U / scales
        offset = 0
        for candidate, idx in layout:
            block = basis[offset:offset + 3 * len(idx)].reshape(len(idx), 3, chosen)
            offset += 3 * len(idx)
            base = np.empty(len(candidate.data.vertices) * 3, dtype=np.float32)
            candidate.data.shape_keys.key_blocks["Basis"].data.foreach_get("co", base)
            base = base.reshape(-1, 3)
            for k in range(chosen):
                shape = base.copy()
                shape[idx] += block[:, :, k]
                key = candidate.shape_key_add(name="%s%d" % (corrective_prefix, k + 1), from_mix=False)
                key.slider_min, key.slider_max = -10.0, 10.0
                key.data.foreach_set("co", shape.astype(np.float32).ravel())
                key.value = 0.0
        for candidate, _ in layout:
            candidate.data.shape_keys.animation_data.action = bpy.data.actions[names["shapes"][candidate.name]]
            for frame in range(1, END + 1):
                for key in candidate.data.shape_keys.key_blocks:
                    if key.name.startswith(corrective_prefix):
                        key.value = float(coefficients[frame - 1, int(key.name[len(corrective_prefix):]) - 1])
                        key.keyframe_insert("value", frame=frame)
        for name in names["shapes"].values():
            linear(bpy.data.actions[name])
        corrective = {"method": "Rest-space inverse weighted LBS deltas per frame, PCA without centering",
                      "prefix": corrective_prefix, "rank": chosen, "target_mm": target_m * 1000,
                      "residual_max_mm": residual * 1000, "residual_by_rank_mm": [round(r * 1000, 2) for r in residuals],
                      "active_vertices": {c.name: int(len(i)) for c, i in layout},
                      "coefficient_range": [float(coefficients.min()), float(coefficients.max())]}
        report = validate(names)
        report["corrective"] = corrective
        report["calibration"] = json.loads(LIFT_PATH.read_text(encoding="utf-8"))["calibration"]
        report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
        result = {"phase": PHASE, "passed": report["passed"], "corrective": {k: v for k, v in corrective.items() if k != "active_vertices"},
                  "worst_mm": max(report["max_vertex_error_metres"].values()) * 1000,
                  "between_mm": max(report["max_between_keys_vertex_error_metres"].values()) * 1000,
                  "angle_deg": report["max_bone_angle_error_degrees"],
                  "per_mesh_mm": {k: round(v * 1000, 2) for k, v in report["max_vertex_error_metres"].items()}}
    elif PHASE == "collect":
        assert LONG
        CHUNKS.mkdir(parents=True, exist_ok=True)
        start, stop = RANGE
        measured = body_meshes()
        hidden = hide_meshes()
        frames_out, src_loc, src_rot, game_rows, low_frames, low_values = [], [], [], [], [], []
        last = {}
        try:
            for frame in range(start, stop + 1):
                pose_at((frame - 1) / FPS, 0.0)
                loc, rot = source_values()
                depsgraph = bpy.context.evaluated_depsgraph_get()
                frames_out.append(frame); src_loc.append(loc); src_rot.append(rot); game_rows.append(game_values(depsgraph, last))
                if frame % 3 == 1 or frame == END:
                    for obj in measured:
                        obj.hide_viewport = False
                    low_frames.append(frame); low_values.append(lowest_point(bpy.context.evaluated_depsgraph_get()))
                    for obj in measured:
                        obj.hide_viewport = True
        finally:
            for obj in hidden:
                obj.hide_viewport = False
        np.savez(CHUNKS / ("collect_%05d.npz" % start), frames=np.array(frames_out), src_loc=np.array(src_loc, dtype=np.float32),
                 src_rot=np.array(src_rot, dtype=np.float32), game=np.array(game_rows, dtype=np.float32),
                 low_frames=np.array(low_frames), low_values=np.array(low_values, dtype=np.float64))
        result = {"phase": PHASE, "range": [start, stop], "lowest_m": float(min(low_values)),
                  "wrist_roll_frames": ROLL_FRAMES, "plane_detours": sorted(set(PLANE_DETOURS))}
    elif PHASE in ("basis_mask", "basis_rows"):
        # Corrective basis inputs in parts (each call under the bridge timeout):
        # mask = vertices that ever differ > 0.3 mm (every 16th frame), rows = rest-space deltas (every 8th).
        assert LONG
        first, last_frame = RANGE
        src_loc, src_rot, game_rows, low_frames, low_values = load_collected()
        lift_path = EVIDENCE / ("%s_lift.npy" % STEM)
        if PHASE == "basis_mask" and first == 1:
            lift = lift_curve(low_frames, low_values)
            np.save(lift_path, lift)
            LIFT_PATH.write_text(json.dumps({"lift_min_m": float(lift.min()), "lift_max_m": float(lift.max()),
                                             "calibration": calibration}, indent=2), encoding="utf-8")
        lift = np.load(lift_path)
        reset_pose()
        corrective_prefix = "Game_" + CLIP + "_"
        for obj in shape_objects():
            if obj.data.shape_keys.animation_data:
                obj.data.shape_keys.animation_data.action = None
            for key in obj.data.shape_keys.key_blocks[1:]:
                key.value = 1.0 if key.name == "Game_NeutralCorrective" else 0.0
        if source.animation_data:
            source.animation_data.action = None
        if game.animation_data:
            game.animation_data.action = None
        originals = {c.name: o for o, c in pairs}
        parts_dir = EVIDENCE / "basis_parts"
        parts_dir.mkdir(exist_ok=True)
        if PHASE == "basis_mask":
            active = {c.name: np.zeros(len(c.data.vertices), dtype=bool) for _, c in pairs}
            for frame in range(first, last_frame + 1):
                if (frame - 1) % 16:
                    continue
                i = frame - 1
                depsgraph = apply_frame(src_loc[i], src_rot[i], game_rows[i], float(lift[i]))
                for original, candidate in pairs:
                    active[candidate.name] |= np.linalg.norm(positions(original, depsgraph) - positions(candidate, depsgraph), axis=1) > 0.0003
            np.savez(parts_dir / ("mask_%05d.npz" % first), **{name: value for name, value in active.items()})
            result = {"phase": PHASE, "range": [first, last_frame]}
        else:
            masks = [np.load(p) for p in sorted(parts_dir.glob("mask_*.npz"))]
            layout = []
            for _, candidate in pairs:
                merged = np.zeros(len(candidate.data.vertices), dtype=bool)
                for m in masks:
                    merged |= m[candidate.name]
                if merged.any():
                    layout.append((candidate, np.nonzero(merged)[0]))
            rest_delta = rest_delta_factory()
            rows_out, frames_out = [], []
            for frame in range(first, last_frame + 1):
                if (frame - 1) % 8:
                    continue
                i = frame - 1
                depsgraph = apply_frame(src_loc[i], src_rot[i], game_rows[i], float(lift[i]))
                parts = []
                for candidate, idx in layout:
                    difference = positions(originals[candidate.name], depsgraph) - positions(candidate, depsgraph)
                    parts.append(rest_delta(candidate, difference, depsgraph, idx).astype(np.float32).ravel())
                rows_out.append(np.concatenate(parts))
                frames_out.append(frame)
            np.savez(parts_dir / ("rows_%05d.npz" % first), rows=np.array(rows_out, dtype=np.float32), frames=np.array(frames_out),
                     names=np.array([c.name for c, _ in layout]), **{"idx_%d" % n: i for n, (_, i) in enumerate(layout)})
            result = {"phase": PHASE, "range": [first, last_frame], "rows": len(rows_out)}
    elif PHASE == "basis_fit":
        # One corrective basis for the whole take: PCA of all row parts, signed shapes, projector.
        assert LONG
        parts = [np.load(p) for p in sorted((EVIDENCE / "basis_parts").glob("rows_*.npz"))]
        names_ref = [str(n) for n in parts[0]["names"]]
        assert all([str(n) for n in p["names"]] == names_ref for p in parts), "row parts use different layouts"
        layout = [(bpy.data.objects[name], parts[0]["idx_%d" % n]) for n, name in enumerate(names_ref)]
        D = np.concatenate([p["rows"] for p in parts]).astype(np.float32)
        corrective_prefix = "Game_" + CLIP + "_"
        for _, candidate in pairs:
            for key in list(candidate.data.shape_keys.key_blocks):
                if key.name.startswith(corrective_prefix):
                    candidate.shape_key_remove(key)
        max_rank = globals().get("MAX_RANK", 64)
        values, vectors = np.linalg.eigh((D @ D.T).astype(np.float64))
        vectors = vectors[:, values.argsort()[::-1]]
        chosen, residuals = max_rank, []
        for rank in range(8, max_rank + 1, 8):
            U = vectors[:, :rank].astype(np.float32)
            projected = U.T @ D
            worst = 0.0
            for block in range(0, len(D), 256):
                rest = D[block:block + 256] - U[block:block + 256] @ projected
                worst = max(worst, float(np.linalg.norm(rest.reshape(len(rest), -1, 3), axis=2).max()))
            residuals.append((rank, worst * 1000))
            if residuals[-1][1] <= 1.0:
                chosen = rank
                break
        U = vectors[:, :chosen]
        scales = np.array([max(1e-9, float(np.abs(U[:, k]).max())) for k in range(chosen)])
        basis = (D.T @ U.astype(np.float32)).astype(np.float64) * scales
        projector = basis @ np.linalg.inv(basis.T @ basis)
        offset = 0
        for candidate, idx in layout:
            block = basis[offset:offset + 3 * len(idx)].reshape(len(idx), 3, chosen)
            offset += 3 * len(idx)
            base = np.empty(len(candidate.data.vertices) * 3, dtype=np.float32)
            candidate.data.shape_keys.key_blocks["Basis"].data.foreach_get("co", base)
            base = base.reshape(-1, 3)
            for k in range(chosen):
                shape = base.copy()
                shape[idx] += block[:, :, k]
                key = candidate.shape_key_add(name="%s%d" % (corrective_prefix, k + 1), from_mix=False)
                key.slider_min, key.slider_max = -10.0, 10.0
                key.data.foreach_set("co", shape.astype(np.float32).ravel())
                key.value = 0.0
        np.savez(BASIS_PATH, projector=projector.astype(np.float32), names=np.array(names_ref),
                 counts=np.array([len(i) for _, i in layout]), **{"idx_%d" % n: i for n, (_, i) in enumerate(layout)})
        result = {"phase": "basis", "rank": chosen, "residual_by_rank_mm": residuals, "rows": int(len(D)),
                  "active_vertices": {c.name: int(len(i)) for c, i in layout}}
    elif PHASE == "basis_restore":
        # Recreate the corrective shapes from the saved projector P = B (B^T B)^-1:  B = P (P^T P)^-1.
        assert LONG
        basis_file = np.load(BASIS_PATH)
        projector = basis_file["projector"].astype(np.float64)
        basis = projector @ np.linalg.inv(projector.T @ projector)
        chosen = basis.shape[1]
        corrective_prefix = "Game_" + CLIP + "_"
        offset = 0
        for n, name in enumerate(basis_file["names"]):
            candidate = bpy.data.objects[str(name)]
            idx = basis_file["idx_%d" % n]
            for key in list(candidate.data.shape_keys.key_blocks):
                if key.name.startswith(corrective_prefix):
                    candidate.shape_key_remove(key)
            block = basis[offset:offset + 3 * len(idx)].reshape(len(idx), 3, chosen)
            offset += 3 * len(idx)
            base = np.empty(len(candidate.data.vertices) * 3, dtype=np.float32)
            candidate.data.shape_keys.key_blocks["Basis"].data.foreach_get("co", base)
            base = base.reshape(-1, 3)
            for k in range(chosen):
                shape = base.copy()
                shape[idx] += block[:, :, k]
                key = candidate.shape_key_add(name="%s%d" % (corrective_prefix, k + 1), from_mix=False)
                key.slider_min, key.slider_max = -10.0, 10.0
                key.data.foreach_set("co", shape.astype(np.float32).ravel())
                key.value = 0.0
        assert offset == basis.shape[0]
        result = {"phase": PHASE, "rank": chosen, "max_offset_m": float(np.abs(basis).max())}
    elif PHASE == "segment_setup":
        # Segment actions from the collected seg_frames (corrective curves are added by segment_finish).
        assert LONG
        number = globals()["SEGMENT"]
        start, stop = segments()[number - 1]
        src_loc, src_rot, game_rows, _, _ = load_collected()
        lift = np.load(EVIDENCE / ("%s_lift.npy" % STEM))
        seg_frames = np.arange(start, stop + 1)
        rows = slice(start - 1, stop)
        seg_name = "%s_S%02d" % (CLIP, number)
        for action in list(bpy.data.actions):
            if action.name.startswith(seg_name + "_"):
                bpy.data.actions.remove(action)
        source.animation_data_create(); game.animation_data_create()
        source_action = bpy.data.actions.new(seg_name + "_Source")
        game_action = bpy.data.actions.new(seg_name + "_Game")
        source_action.use_fake_user = game_action.use_fake_user = True
        source.animation_data.action = source_action
        game.animation_data.action = game_action
        torso_loc = src_loc[rows].astype(np.float64).copy()
        torso_loc[:, 2] += lift[rows]
        for axis in range(3):
            bulk_curve(source, source_action, 'pose.bones["torso"].location', axis, seg_frames, torso_loc[:, axis], "torso")
        for k, name in enumerate(SOURCE_KEYS):
            bone = PB[name]
            values_k = src_rot[rows, k].astype(np.float64)
            if bone.rotation_mode == "QUATERNION":
                values_k = continuous(values_k)
                for axis in range(4):
                    bulk_curve(source, source_action, 'pose.bones["%s"].rotation_quaternion' % name, axis, seg_frames, values_k[:, axis], name)
            else:
                for axis in range(3):
                    bulk_curve(source, source_action, 'pose.bones["%s"].rotation_euler' % name, axis, seg_frames, values_k[:, axis], name)
        for b, bone in enumerate(ordered):
            values_b = game_rows[rows, b].astype(np.float64).copy()
            if bone.name in TOP_GAME_BONES:
                values_b[:, 0:3] += np.outer(lift[rows], np.array(bone.bone.matrix_local.to_3x3().inverted() @ Vector((0.0, 0.0, 1.0))))
            values_b[:, 3:7] = continuous(values_b[:, 3:7])
            path = 'pose.bones["%s"].' % bone.name
            for axis in range(3):
                bulk_curve(game, game_action, path + "location", axis, seg_frames, values_b[:, axis], bone.name)
                bulk_curve(game, game_action, path + "scale", axis, seg_frames, values_b[:, 7 + axis], bone.name)
            for axis in range(4):
                bulk_curve(game, game_action, path + "rotation_quaternion", axis, seg_frames, values_b[:, 3 + axis], bone.name)
        corrective_prefix = "Game_" + CLIP + "_"
        shape_names = {}
        face = np.array([[face_values(int(round(((f - 1) / FPS) * RATE)))[key] for key in FACE_MAP] for f in seg_frames])
        for obj in shape_objects():
            keys = obj.data.shape_keys
            keys.animation_data_create()
            action = bpy.data.actions.new(seg_name + "_Shapes_" + obj.name)
            action.use_fake_user = True
            keys.animation_data.action = action
            shape_names[obj.name] = action.name
            for key in keys.key_blocks[1:]:
                path = 'key_blocks["%s"].value' % key.name
                if key.name in FACE_MAP:
                    bulk_curve(keys, action, path, 0, seg_frames, face[:, list(FACE_MAP).index(key.name)], key.name)
                elif not key.name.startswith(corrective_prefix):
                    constant = 1.0 if key.name == "Game_NeutralCorrective" else 0.0
                    bulk_curve(keys, action, path, 0, np.array([start, stop]), np.array([constant, constant]), key.name)
        names = {"source": source_action.name, "game": game_action.name, "shapes": shape_names, "range": [start, stop]}
        (EVIDENCE / ("%s_actions.json" % seg_name.lower())).write_text(json.dumps(names, indent=2), encoding="utf-8")
        result = {"phase": PHASE, "segment": number, "range": [start, stop]}
    elif PHASE == "segment_coef":
        # Corrective coefficients for part of a segment: projection of the rest-space delta on the basis.
        assert LONG
        number = globals()["SEGMENT"]
        seg_name = "%s_S%02d" % (CLIP, number)
        names = json.loads((EVIDENCE / ("%s_actions.json" % seg_name.lower())).read_text(encoding="utf-8"))
        first, last_frame = RANGE
        assign(names)
        reset_pose()
        scene.render.fps, scene.render.fps_base = FPS, 1
        basis = np.load(BASIS_PATH)
        projector = basis["projector"].astype(np.float64)
        layout = [(bpy.data.objects[str(name)], basis["idx_%d" % n]) for n, name in enumerate(basis["names"])]
        originals = {c.name: o for o, c in pairs}
        rest_delta = rest_delta_factory()
        coefficients = []
        for frame in range(first, last_frame + 1):
            depsgraph = frame_set(frame)
            parts = []
            for candidate, idx in layout:
                difference = positions(originals[candidate.name], depsgraph) - positions(candidate, depsgraph)
                parts.append(rest_delta(candidate, difference, depsgraph, idx).ravel())
            coefficients.append(np.concatenate(parts) @ projector)
        (EVIDENCE / "coef").mkdir(exist_ok=True)
        np.savez(EVIDENCE / "coef" / ("%s_%05d.npz" % (seg_name, first)), seg_frames=np.arange(first, last_frame + 1), values=np.array(coefficients))
        result = {"phase": PHASE, "segment": number, "range": [first, last_frame]}
    elif PHASE == "segment_finish":
        # Corrective curves from the coefficient parts, then whole/half-frame validation.
        assert LONG
        number = globals()["SEGMENT"]
        seg_name = "%s_S%02d" % (CLIP, number)
        names = json.loads((EVIDENCE / ("%s_actions.json" % seg_name.lower())).read_text(encoding="utf-8"))
        start, stop = names["range"]
        parts = [np.load(p) for p in sorted((EVIDENCE / "coef").glob("%s_*.npz" % seg_name))]
        seg_frames = np.concatenate([p["seg_frames"] for p in parts])
        coefficients = np.concatenate([p["values"] for p in parts])
        assert np.array_equal(seg_frames, np.arange(start, stop + 1)), "coefficient parts incomplete"
        corrective_prefix = "Game_" + CLIP + "_"
        basis = np.load(BASIS_PATH)
        assign(names)
        for name in basis["names"]:
            keys = bpy.data.objects[str(name)].data.shape_keys
            for key in keys.key_blocks:
                if key.name.startswith(corrective_prefix):
                    k = int(key.name[len(corrective_prefix):]) - 1
                    bulk_curve(keys, bpy.data.actions[names["shapes"][str(name)]], 'key_blocks["%s"].value' % key.name, 0, seg_frames,
                               coefficients[:, k], key.name)
        report = validate_range(names, start, stop)
        report.update(segment=number, coefficient_range=[float(coefficients.min()), float(coefficients.max())],
                      allowed_failed=bool(globals().get("ALLOW_FAILED")) and not report["passed"])
        (EVIDENCE / ("%s_S%02d_validation.json" % (STEM, number))).write_text(json.dumps(report, indent=2), encoding="utf-8")
        result = {"phase": PHASE, "segment": number, "passed": report["passed"],
                  "worst_mm": max(report["max_vertex_error_metres"].values()) * 1000,
                  "between_mm": max(report["max_between_keys_vertex_error_metres"].values()) * 1000,
                  "angle_deg": report["max_bone_angle_error_degrees"]}
    elif PHASE == "segment_export":
        assert LONG
        number = globals()["SEGMENT"]
        seg_name = "%s_S%02d" % (CLIP, number)
        names = json.loads((EVIDENCE / ("%s_actions.json" % seg_name.lower())).read_text(encoding="utf-8"))
        path = EVIDENCE / ("%s_S%02d_validation.json" % (STEM, number))
        report = json.loads(path.read_text(encoding="utf-8"))
        assert report["passed"] or report.get("allowed_failed"), "segment failed validation"
        report.update(export_range(names, names["range"][0], names["range"][1], "Gratia_Game_%s_S%02d.fbx" % (CLIP, number)))
        path.write_text(json.dumps(report, indent=2), encoding="utf-8")
        if not globals().get("KEEP_ACTIONS"):
            for name in [names["source"], names["game"]] + list(names["shapes"].values()):
                bpy.data.actions.remove(bpy.data.actions[name])
        result = {"phase": PHASE, "segment": number, "fbx": report["fbx"], "bytes": report["bytes"]}
    elif PHASE == "save":
        backup = EVIDENCE / "Gratia_mvp_before_km466_v3.blend"
        if not backup.exists():
            # The pre-change state is the file on disk (verified SHA in MOCAP_KM466.md).
            import shutil
            shutil.copyfile(ROOT / "Exports/Gratia/Gratia_mvp.blend", backup)
        restore()
        assert "FINISHED" in bpy.ops.wm.save_as_mainfile(filepath=str(ROOT / "Exports/Gratia/Gratia_mvp.blend"), check_existing=False)
        with (ROOT / "Exports/Gratia/Gratia_mvp.blend").open("rb") as stream:
            result = {"phase": PHASE, "saved_sha256": hashlib.file_digest(stream, "sha256").hexdigest(), "backup": str(backup)}
    elif PHASE == "export":
        report = json.loads(report_path.read_text(encoding="utf-8"))
        assert report["passed"] or globals().get("ALLOW_FAILED"), "Stop before export: clip failed validation"
        names = action_names()
        assign(names)
        scene.render.fps, scene.render.fps_base = FPS, 1
        scene.frame_start, scene.frame_end = 1, END
        frame_set(1)
        selected = [game] + [candidate for _, candidate in pairs]
        for obj in scene.objects:
            obj.select_set(False)
        for obj in selected:
            obj.select_set(True)
        bpy.context.view_layer.objects.active = game
        path = OUT / ("Gratia_Game_%s.fbx" % CLIP)
        with bpy.context.temp_override(active_object=game, object=game, selected_objects=selected, selected_editable_objects=selected):
            assert "FINISHED" in bpy.ops.export_scene.fbx(
                filepath=str(path), use_selection=True, object_types={"MESH", "ARMATURE"}, global_scale=1,
                apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS", axis_forward="-Y", axis_up="Z",
                use_mesh_modifiers=False, mesh_smooth_type="FACE", add_leaf_bones=False,
                use_armature_deform_only=True, armature_nodetype="NULL", path_mode="AUTO", embed_textures=False,
                bake_anim=True, bake_anim_use_all_bones=True, bake_anim_use_nla_strips=False,
                bake_anim_use_all_actions=False, bake_anim_force_startend_keying=True,
                bake_anim_step=1, bake_anim_simplify_factor=0)
        with path.open("rb") as stream:
            report.update(exported=True, fbx=str(path), bytes=path.stat().st_size, sha256=hashlib.file_digest(stream, "sha256").hexdigest())
        report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
        result = {"phase": PHASE, "fbx": str(path), "bytes": report["bytes"], "sha256": report["sha256"]}
    else:
        raise RuntimeError("Unknown PHASE: " + str(PHASE))
finally:
    restore()
result["elapsed_seconds"] = time.time() - started
(EVIDENCE / ("%s_%s.json" % (STEM, PHASE))).write_text(json.dumps(result, indent=2, default=str), encoding="utf-8")
print(json.dumps(result, default=str)[:6000])
