"""Expressive anime-style clips (Zenless Zone Zero-like timing), executed only through Blender MCP.

Timing language: a short anticipation, a fast underdamped snap into a readable pose
(overshoot), staggered follow-through per body part (hips lead, then chest, head,
arms, hands, ears/tail), a living hold and a springy return to the shared neutral.
Facial shapes switch quickly, anime style.

PHASE "preview": render the peak pose of each clip (no keys, nothing saved).
PHASE "author": key the source rig, bake the clean game rig per frame, validate
deformation against the source evaluated meshes and save the editable copy.
PHASE "export": export validated clips (game rig + candidate meshes for morph curves).
Existing actions, shape keys and exports are not replaced.
"""
import bpy
import hashlib
import json
import math
from pathlib import Path

import numpy as np
from mathutils import Euler, Matrix, Quaternion, Vector

ROOT = Path("E:/coding/ue proto")
OUT = ROOT / "Exports/Gratia/GameRig"
EVIDENCE = ROOT / "evidence/05/blender_mcp"
EVIDENCE.mkdir(parents=True, exist_ok=True)
PHASE = globals().get("PHASE", "preview")
ONLY = globals().get("ONLY")
assert Path(bpy.data.filepath).resolve() == (ROOT / "Exports/Gratia/Gratia_mvp.blend").resolve()
for filename, expected in [
    ("Gratia.blend", "B4C98285167829C824194E5E5A9F8D4BE14AAE7C377C2CCE4E37F19BC4B88DC7"),
    ("Gratia_source.blend", "B4C98285167829C824194E5E5A9F8D4BE14AAE7C377C2CCE4E37F19BC4B88DC7"),
    ("Gratia_working.blend", "C455FD7983B1360DAC1006E8B549B91263CC7396EDC98E9D2A1B72B506A4F9A5"),
]:
    with (ROOT / filename).open("rb") as stream:
        assert hashlib.file_digest(stream, "sha256").hexdigest().upper() == expected, filename

source = bpy.data.objects["Gratia"]
game = bpy.data.objects["Gratia_GameRig"]
scene = bpy.context.scene
scene.render.fps, scene.render.fps_base = 30, 1
manifest = json.loads((OUT / "game_rig_manifest.json").read_text(encoding="utf-8"))
pairs = [(bpy.data.objects[item["source"]], bpy.data.objects[item["candidate"]]) for item in manifest["meshes"]]
report_path = EVIDENCE / "anime_clips_validation.json"

ARMS = ["upper_arm_fk.L", "upper_arm_fk.R", "forearm_fk.L", "forearm_fk.R", "hand_fk.L", "hand_fk.R"]
BODY = ["torso", "hips", "chest", "spine_fk.003", "neck", "head"]
EARS = [n for n in ("ear.L", "ear.L.001", "ear.L.002", "ear.R", "ear.R.001", "ear.R.002") if n in source.pose.bones]
TAIL = [n for n in ("tail",) if n in source.pose.bones]
FINGERS = [bone.name for bone in source.pose.bones if "_master." in bone.name]
CONTROLS = BODY + ARMS + FINGERS + EARS + TAIL
for name in CONTROLS:
    assert source.pose.bones[name].rotation_mode == "QUATERNION", name
FEET = ["foot_ik.L", "foot_ik.R"]


# ---------------------------------------------------------------- timing
def spring_step(t, freq, zeta):
    """Underdamped step response 0 -> 1 (overshoot ~exp(-zeta*pi/sqrt(1-zeta^2)))."""
    if t <= 0:
        return 0.0
    wn = 2 * math.pi * freq
    root = math.sqrt(1 - zeta * zeta)
    wd = wn * root
    return 1 - math.exp(-zeta * wn * t) * (math.cos(wd * t) + zeta / root * math.sin(wd * t))


def bump(t, start, end):
    """Smooth 0 -> 1 -> 0 over [start, end]."""
    if t <= start or t >= end:
        return 0.0
    x = (t - start) / (end - start)
    return math.sin(math.pi * x) ** 2


def smooth(x):
    x = min(1.0, max(0.0, x))
    return x * x * (3 - 2 * x)


def envelope(t, clip, delay=0.0):
    """Anticipation, snap in, hold, springy return; exactly 0 at both ends."""
    t -= delay
    spec = clip["timing"]
    value = -spec["anticipation"] * bump(t, spec["in"] - spec["anticipation_time"], spec["in"] + 0.02)
    value += spring_step(t - spec["in"], spec["in_freq"], spec["in_zeta"])
    value -= spring_step(t - spec["out"], spec["out_freq"], spec["out_zeta"])
    end = clip["duration"]
    return value * (1 - smooth((t - (end - 0.2 - delay)) / 0.2)) * smooth(t / 0.05 + 1) if t > -1 else 0.0


# ---------------------------------------------------------------- rig helpers
def local_axis(name, axis):
    return source.pose.bones[name].bone.matrix_local.to_quaternion().inverted() @ Vector(axis)


def world_rotation(name, axis, degrees):
    return Quaternion(local_axis(name, axis), math.radians(degrees))


def scaled(delta, weight):
    axis, angle = delta.to_axis_angle()
    return Quaternion(axis, angle * weight)


def frame_set(frame):
    scene.frame_set(math.floor(frame), subframe=frame % 1)
    bpy.context.view_layer.update()
    return bpy.context.evaluated_depsgraph_get()


def solve_arm(side, target, pole, hand_axis):
    """Two-bone FK solve to a world target; the palm follows the forearm roll."""
    upper = source.pose.bones["upper_arm_fk." + side]
    lower = source.pose.bones["forearm_fk." + side]
    wrist = source.pose.bones["hand_fk." + side]
    shoulder = upper.matrix.translation.copy()
    length_a, length_b = upper.bone.length, lower.bone.length
    relative = Vector(target) - shoulder
    distance = min(length_a + length_b - 0.0001, max(abs(length_a - length_b) + 0.0001, relative.length))
    forward = relative.normalized()
    plane = Vector(pole) - shoulder
    plane = (plane - forward * plane.dot(forward)).normalized()
    along = (length_a * length_a - length_b * length_b + distance * distance) / (2 * distance)
    elbow = shoulder + forward * along + plane * math.sqrt(max(0, length_a * length_a - along * along))
    hand = shoulder + forward * distance
    for control, begin, end in [(upper, shoulder, elbow), (lower, elbow, hand)]:
        rest = control.bone.matrix_local
        direction = (control.bone.tail_local - control.bone.head_local).normalized()
        rotation = direction.rotation_difference((end - begin).normalized()) @ rest.to_quaternion()
        control.matrix = Matrix.LocRotScale(begin, rotation, Vector((1, 1, 1)))
        bpy.context.view_layer.update()
    rest_wrist = wrist.bone.matrix_local.to_quaternion()
    rest_axis = (wrist.bone.tail_local - wrist.bone.head_local).normalized()
    inherited = lower.matrix.to_quaternion() @ lower.bone.matrix_local.to_quaternion().inverted() @ rest_wrist
    inherited_axis = inherited @ (rest_wrist.inverted() @ rest_axis)
    wrist.matrix = Matrix.LocRotScale(hand, inherited_axis.rotation_difference(Vector(hand_axis).normalized()) @ inherited, Vector((1, 1, 1)))
    bpy.context.view_layer.update()
    return {"side": side, "target": list(target), "reached": list(hand), "elbow": list(elbow)}


# ---------------------------------------------------------------- clips
# Character faces -Y, up +Z, her left is +X. World-axis rotations: +X nods forward,
# +Z turns to her left, +Y tilts the top of the head to her left.
CLIPS = {
    "IdleAnime": {"duration": 8.0, "loop": True, "fps": 30},
    "ReactShy": {
        "fps": 60,
        "duration": 2.6,
        "timing": dict(anticipation=0.18, anticipation_time=0.12, **{"in": 0.12}, in_freq=3.0, in_zeta=0.5,
                       out=1.65, out_freq=1.9, out_zeta=0.62),
        "rot": {"hips": [((1, 0, 0), -3)], "chest": [((1, 0, 0), 7), ((0, 0, 1), -8), ((0, 1, 0), 3)],
                "neck": [((1, 0, 0), 4)], "head": [((0, 0, 1), -20), ((1, 0, 0), 9), ((0, 1, 0), 7)],
                "ear.L": [((1, 0, 0), -16), ((0, 1, 0), 22)], "ear.R": [((1, 0, 0), -16), ((0, 1, 0), -22)],
                "ear.L.001": [((0, 1, 0), 10)], "ear.R.001": [((0, 1, 0), -10)], "tail": [((1, 0, 0), 25)]},
        "loc": {"torso": (0.0, 0.009, 0.0)},
        "arms": {"L": ((0.045, -0.18, 1.445), (0.33, 0.05, 1.18), (-0.45, -0.25, 0.86)),
                 "R": ((-0.035, -0.19, 1.425), (-0.33, 0.05, 1.18), (0.45, -0.25, 0.86))},
        "fingers": (34, 0.78),
        "face": {"Brows worry 1": 0.85, "Eyes worry": 0.55, "Mouth nervous 1": 0.75, "Look right": 0.7},
    },
    "ReactHappy": {
        "fps": 60,
        "duration": 2.4,
        "timing": dict(anticipation=0.35, anticipation_time=0.14, **{"in": 0.14}, in_freq=3.4, in_zeta=0.42,
                       out=1.5, out_freq=2.0, out_zeta=0.58),
        "rot": {"hips": [((0, 1, 0), 3)], "chest": [((1, 0, 0), -6), ((0, 1, 0), -3)],
                "head": [((0, 1, 0), 13), ((1, 0, 0), -5), ((0, 0, 1), 4)],
                "ear.L": [((1, 0, 0), 10), ((0, 1, 0), -6)], "ear.R": [((1, 0, 0), 10), ((0, 1, 0), 6)],
                "tail": [((1, 0, 0), -18)]},
        "loc": {"torso": (0.0, 0.0, 0.0)},
        "arms": {"L": ((0.2, -0.16, 1.58), (0.42, 0.02, 1.26), (0.0, -0.25, 1.0)),
                 "R": ((-0.2, -0.16, 1.58), (-0.42, 0.02, 1.26), (0.0, -0.25, 1.0))},
        "fingers": (48, 0.66),
        "face": {"Eye L close": 1.0, "Eye R close": 1.0, "Mouth happy 2": 0.9, "Brows up": 0.45},
    },
    "ReactPout": {
        "fps": 60,
        "duration": 2.6,
        "timing": dict(anticipation=0.2, anticipation_time=0.1, **{"in": 0.1}, in_freq=3.2, in_zeta=0.5,
                       out=1.75, out_freq=2.0, out_zeta=0.6),
        "rot": {"hips": [((0, 1, 0), -4), ((0, 0, 1), 5)], "chest": [((1, 0, 0), -3), ((0, 1, 0), 2), ((0, 0, 1), 6)],
                "head": [((0, 0, 1), 24), ((1, 0, 0), -7), ((0, 1, 0), -5)],
                "ear.L": [((1, 0, 0), -24)], "ear.R": [((1, 0, 0), -24)], "tail": [((0, 0, 1), 20)]},
        "loc": {"torso": (0.009, 0.0, 0.0)},
        "arms": {"L": ((0.175, -0.015, 1.04), (0.42, 0.16, 1.24), (-0.35, -0.55, -0.75)),
                 "R": ((-0.175, -0.015, 1.04), (-0.42, 0.16, 1.24), (0.35, -0.55, -0.75))},
        "fingers": (14, 0.94),
        "face": {"Mouth puff out": 0.9, "Brows angry 1": 0.75, "Eyes suspecting": 0.5, "Look left": 0.6},
    },
    "ReactStartle": {
        "fps": 60,
        "duration": 1.9,
        "timing": dict(anticipation=0.08, anticipation_time=0.05, **{"in": 0.05}, in_freq=4.4, in_zeta=0.38,
                       out=0.85, out_freq=1.8, out_zeta=0.6),
        "rot": {"hips": [((1, 0, 0), 3)], "chest": [((1, 0, 0), -7)], "neck": [((1, 0, 0), -3)],
                "head": [((1, 0, 0), -8), ((0, 0, 1), -5)],
                "ear.L": [((1, 0, 0), 14)], "ear.R": [((1, 0, 0), 14)], "tail": [((1, 0, 0), -30)]},
        "loc": {"torso": (0.0, 0.01, 0.0)},
        "arms": {"L": ((0.16, -0.2, 1.47), (0.42, 0.04, 1.2), (0.3, -0.35, 0.88)),
                 "R": ((-0.16, -0.2, 1.47), (-0.42, 0.04, 1.2), (-0.3, -0.35, 0.88))},
        "fingers": (-6, 1.0),
        "face": {"Eyes surprised": 1.0, "Mouth o": 0.85, "Brows up": 1.0},
    },
}
# Follow-through: later parts react later; ears/tail lag most.
DELAYS = {"torso": 0.0, "hips": 0.0, "chest": 0.025, "spine_fk.003": 0.025, "neck": 0.04, "head": 0.05,
          "arm": 0.05, "hand": 0.075, "finger": 0.09, "ear": 0.1, "tail": 0.11, "face": -0.02}


def group(name):
    if name in DELAYS: return name
    if name.startswith(("upper_arm", "forearm")): return "arm"
    if name.startswith("hand_fk"): return "hand"
    if "_master." in name: return "finger"
    if name.startswith("ear"): return "ear"
    if name.startswith("tail"): return "tail"
    return "chest"


def idle_pose(t, neutral):
    """8 s loop: weight shift, breathing, curious head tilt, ear twitches, tail sway."""
    period = CLIPS["IdleAnime"]["duration"]
    w = 2 * math.pi * t / period
    breath = math.sin(2 * w)
    sway = math.sin(w)
    rot = {
        "hips": [((0, 1, 0), 2.6 * sway), ((0, 0, 1), 1.2 * math.sin(w + 0.6))],
        "chest": [((0, 1, 0), -2.0 * math.sin(w + 0.35)), ((1, 0, 0), -1.1 * breath)],
        "neck": [((0, 1, 0), -0.8 * math.sin(w + 0.7))],
        "head": [((0, 1, 0), 7.5 * bump(t, 2.6, 5.4) - 1.0 * math.sin(w + 0.9)),
                 ((0, 0, 1), 4.0 * math.sin(w + 1.2) - 3.0 * bump(t, 5.6, 7.6)),
                 ((1, 0, 0), 2.0 * bump(t, 2.6, 5.4) + 0.4 * breath)],
        "ear.L": [((1, 0, 0), 14 * bump(t, 5.2, 5.55) + 3 * math.sin(w + 1.0))],
        "ear.R": [((1, 0, 0), 12 * bump(t, 6.1, 6.45) + 3 * math.sin(w + 1.3)), ((0, 1, 0), -6 * bump(t, 6.1, 6.45))],
        "tail": [((0, 0, 1), 9 * math.sin(2 * w + 0.4))],
        "upper_arm_fk.L": [((1, 0, 0), -2.0 * math.sin(w + 0.9)), ((0, 1, 0), 1.0 * breath)],
        "upper_arm_fk.R": [((1, 0, 0), 2.0 * math.sin(w + 0.9)), ((0, 1, 0), -1.0 * breath)],
        "forearm_fk.L": [((1, 0, 0), -1.5 * math.sin(w + 1.2))],
        "forearm_fk.R": [((1, 0, 0), 1.5 * math.sin(w + 1.2))],
    }
    loc = {"torso": (0.009 * sway, 0.0, 0.0)}
    face = {"Mouth smile": 0.18 + 0.08 * bump(t, 2.6, 5.4), "Brows up": 0.12 * bump(t, 2.6, 5.4)}
    return rot, loc, face


def neutral_state():
    """Shared neutral: Idle frame 1 (= TestHead frame 1, arms lowered), body/ears/tail/feet at rest.

    Only the Idle actions are saved in the working copy (unassigned actions have no users).
    """
    source_actions = json.loads(game["mvp_source_actions"])
    source.animation_data.action = bpy.data.actions[source_actions["Idle"]]
    frame_set(1)
    neutral = {name: source.pose.bones[name].rotation_quaternion.copy() for name in CONTROLS}
    source.animation_data.action = None
    for name in ["hand_fk.L", "hand_fk.R"] + FINGERS + EARS + TAIL + ["torso", "hips", "chest", "neck"]:
        neutral[name] = Quaternion((1, 0, 0, 0))
    for name in FINGERS:
        source.pose.bones[name].scale = Vector((1, 1, 1))
    for name in ["torso"] + FEET:
        source.pose.bones[name].location = Vector()
        source.pose.bones[name].rotation_quaternion = Quaternion()
    for name in CONTROLS:
        source.pose.bones[name].rotation_quaternion = neutral[name]
    bpy.context.view_layer.update()
    return neutral


def apply_body(neutral, rot, loc, weight=lambda name: 1.0):
    for name in BODY + EARS + TAIL:
        delta = Quaternion()
        for axis, degrees in rot.get(name, []):
            delta = delta @ world_rotation(name, axis, degrees)
        source.pose.bones[name].rotation_quaternion = neutral[name] @ scaled(delta, weight(name))
    for name, offset in loc.items():
        bone = source.pose.bones[name]
        world = Vector(offset) * weight(name)
        bone.location = bone.bone.matrix_local.to_3x3().inverted() @ world
    bpy.context.view_layer.update()


def peak_arms(clip, neutral):
    """Solve the arm targets on top of the peak body pose; return per-control peak quaternions."""
    apply_body(neutral, clip["rot"], clip["loc"])
    solves = []
    for side, (target, pole, hand_axis) in clip["arms"].items():
        solves.append(solve_arm(side, target, pole, hand_axis))
    peaks = {name: source.pose.bones[name].rotation_quaternion.copy() for name in ARMS}
    for name in ARMS:
        source.pose.bones[name].rotation_quaternion = neutral[name]
    apply_body(neutral, {}, {name: (0, 0, 0) for name in clip["loc"]})
    return peaks, solves


def set_clip_frame(clip_name, clip, t, neutral, arm_peaks):
    """Pose all controls at time t; returns facial shape values."""
    if clip.get("loop"):
        rot, loc, face = idle_pose(t, neutral)
        apply_body(neutral, rot, loc)
        for name in ARMS:
            delta = Quaternion()
            for axis, degrees in rot.get(name, []):
                delta = delta @ world_rotation(name, axis, degrees)
            source.pose.bones[name].rotation_quaternion = neutral[name] @ delta
        for name in FINGERS:
            source.pose.bones[name].rotation_quaternion = neutral[name] @ Quaternion((1, 0, 0), math.radians(9))
            source.pose.bones[name].scale = Vector((1, 0.94, 1))
        bpy.context.view_layer.update()
        return face
    amount = {name: envelope(t, clip, DELAYS[group(name)]) for name in CONTROLS}
    rot = dict(clip["rot"])
    extra = []
    if clip_name == "ReactShy":
        # Trembling hold, then a peek back at the player before the release.
        tremble = 1.2 * math.sin(2 * math.pi * 7 * t) * bump(t, 0.35, 1.3)
        extra = [("head", (0, 0, 1), tremble + 11 * bump(t, 1.05, 1.75)), ("head", (1, 0, 0), -4 * bump(t, 1.05, 1.75))]
    elif clip_name == "ReactHappy":
        wiggle = math.sin(2 * math.pi * 2.6 * (t - 0.3)) * bump(t, 0.3, 1.5)
        extra = [("hips", (0, 1, 0), 4 * wiggle), ("chest", (0, 1, 0), -3 * wiggle), ("head", (0, 1, 0), 3 * wiggle),
                 ("ear.L", (1, 0, 0), 10 * wiggle), ("ear.R", (1, 0, 0), -10 * wiggle),
                 ("tail", (0, 0, 1), 22 * math.sin(2 * math.pi * 4.5 * t) * bump(t, 0.25, 1.6))]
    elif clip_name == "ReactPout":
        glance = bump(t, 1.25, 2.05)
        extra = [("head", (0, 0, 1), -20 * glance), ("head", (1, 0, 0), 4 * glance),
                 ("tail", (0, 0, 1), 12 * math.sin(2 * math.pi * 3 * t) * bump(t, 0.3, 1.2))]
    elif clip_name == "ReactStartle":
        extra = [("ear.L", (0, 1, 0), 6 * math.sin(2 * math.pi * 9 * t) * bump(t, 0.1, 0.7)),
                 ("ear.R", (0, 1, 0), -6 * math.sin(2 * math.pi * 9 * t) * bump(t, 0.1, 0.7))]
    for name in BODY + EARS + TAIL:
        delta = Quaternion()
        for axis, degrees in rot.get(name, []):
            delta = delta @ world_rotation(name, axis, degrees)
        value = neutral[name] @ scaled(delta, amount[name])
        for target, axis, degrees in extra:
            if target == name:
                value = value @ world_rotation(name, axis, degrees)
        source.pose.bones[name].rotation_quaternion = value
    for name, offset in clip["loc"].items():
        bone = source.pose.bones[name]
        bone.location = bone.bone.matrix_local.to_3x3().inverted() @ (Vector(offset) * amount[name])
    for name in ARMS:
        delta = neutral[name].inverted() @ arm_peaks[name]
        source.pose.bones[name].rotation_quaternion = neutral[name] @ scaled(delta, amount[name])
    degrees, scale_y = clip["fingers"]
    for name in FINGERS:
        weight = max(0.0, amount[name])
        thumb = name.startswith("thumb")
        source.pose.bones[name].rotation_quaternion = neutral[name] @ Quaternion((1, 0, 0), math.radians((degrees * (0.55 if thumb else 1.0)) * weight))
        source.pose.bones[name].scale = Vector((1, 1 + (scale_y - 1) * weight * (0.5 if thumb else 1.0), 1))
    bpy.context.view_layer.update()
    face_amount = min(1.0, max(0.0, envelope(t, clip, DELAYS["face"])))
    face = {shape: value * face_amount for shape, value in clip["face"].items()}
    # Anime blink on the snap and before the return.
    blink = max(bump(t, clip["timing"]["in"] - 0.02, clip["timing"]["in"] + 0.16),
                bump(t, clip["timing"]["out"] - 0.1, clip["timing"]["out"] + 0.06))
    if clip_name != "ReactHappy":
        face["Eye L close"] = max(face.get("Eye L close", 0.0), blink)
        face["Eye R close"] = max(face.get("Eye R close", 0.0), blink)
    if clip_name == "ReactShy":
        peek = bump(t, 1.05, 1.75)
        face["Look right"] = face.get("Look right", 0.0) * (1 - peek)
        face["Mouth smile"] = 0.35 * peek
    if clip_name == "ReactPout":
        wink = bump(t, 1.55, 2.2)
        face["Eye R wink"] = wink
        face["Mouth smile"] = 0.55 * wink
        face["Mouth puff out"] = face.get("Mouth puff out", 0.0) * (1 - bump(t, 1.45, 2.4))
        face["Look left"] = face.get("Look left", 0.0) * (1 - bump(t, 1.25, 2.2))
    if clip_name == "ReactStartle":
        after = bump(t, 0.75, 1.75)
        face["Mouth nervous 2"] = 0.55 * after
        face["Brows worry 1"] = 0.6 * after
    return {k: min(1.0, max(0.0, v)) for k, v in face.items()}


def render_views(prefix, views):
    saved = {"camera": scene.camera, "engine": scene.render.engine, "path": scene.render.filepath,
             "res": (scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage)}
    shading = scene.display.shading
    saved_shading = (shading.light, shading.color_type)
    hide = {o.name: o.hide_render for o in scene.objects}
    camera_data = bpy.data.cameras.new("ClaudePreviewCam")
    camera = bpy.data.objects.new("ClaudePreviewCam", camera_data)
    scene.collection.objects.link(camera)
    files = []
    try:
        for obj in scene.objects:
            if obj.type == "MESH":
                obj.hide_render = hide[obj.name] or obj.name.endswith("_GameRig") or obj.name in ("Sword",)
        scene.camera = camera
        scene.render.engine = "BLENDER_WORKBENCH"
        shading.light, shading.color_type = "STUDIO", "MATERIAL"
        scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage = 520, 680, 100
        for name, location, rotation, lens in views:
            camera.location = Vector(location)
            camera.rotation_euler = Euler([math.radians(a) for a in rotation])
            camera_data.lens = lens
            scene.render.filepath = str(EVIDENCE / f"{prefix}_{name}.png")
            bpy.ops.render.render(write_still=True)
            files.append(scene.render.filepath)
    finally:
        for obj_name, value in hide.items():
            if obj_name in scene.objects:
                scene.objects[obj_name].hide_render = value
        scene.camera = saved["camera"]
        scene.render.engine = saved["engine"]
        scene.render.filepath = saved["path"]
        scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage = saved["res"]
        shading.light, shading.color_type = saved_shading
        bpy.data.objects.remove(camera)
        bpy.data.cameras.remove(camera_data)
    return files


VIEWS = [("front", (0, -3.0, 1.12), (90, 0, 0), 50), ("three_quarter", (1.9, -2.4, 1.2), (88, 0, 38), 50)]


def depth(bone):
    return 0 if not bone.parent else depth(bone.parent) + 1


ordered = sorted(game.pose.bones, key=lambda b: depth(b.bone))


def positions(obj, depsgraph):
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    coords = np.empty(len(mesh.vertices) * 3, dtype=np.float32)
    mesh.vertices.foreach_get("co", coords)
    world = np.array(evaluated.matrix_world, dtype=np.float64)
    evaluated.to_mesh_clear()
    return coords.reshape((-1, 3)).astype(np.float64) @ world[:3, :3].T + world[:3, 3]


def curves(action):
    return [curve for layer in action.layers for strip in layer.strips for bag in strip.channelbags for curve in bag.fcurves]


def linear(action):
    for curve in curves(action):
        for key in curve.keyframe_points:
            key.interpolation = "LINEAR"


def use_fps(clip):
    scene.render.fps, scene.render.fps_base = clip.get("fps", 30), 1
    return clip.get("fps", 30)


def assign(source_action, game_action, shape_actions):
    source.animation_data.action = bpy.data.actions[source_action]
    game.animation_data.action = bpy.data.actions[game_action]
    for original, candidate in pairs:
        for obj in (original, candidate):
            if obj.name in shape_actions:
                obj.data.shape_keys.animation_data.action = bpy.data.actions[shape_actions[obj.name]]


def validate(clip_name, clip, end, source_action, game_action, shape_actions, corrective=None):
    """Whole and half frames against the source evaluated meshes (DQS/B-Bones kept in the source)."""
    use_fps(clip)
    assign(source_action, game_action, shape_actions)
    max_errors = {original.name: 0.0 for original, _ in pairs}
    between_errors = {original.name: 0.0 for original, _ in pairs}
    max_angle, foot_drift, samples = 0.0, 0.0, 0
    worst_bones = {}
    reference = None
    for frame in np.arange(1, end + 0.1, 0.5):
        depsgraph = frame_set(float(frame))
        eval_source = source.evaluated_get(depsgraph)
        eval_game = game.evaluated_get(depsgraph)
        feet = {n: eval_game.pose.bones[n].matrix.translation.copy() for n in ("root", "DEF-foot.L", "DEF-foot.R")}
        reference = reference or feet
        foot_drift = max(foot_drift, max((feet[n] - reference[n]).length for n in feet))
        for bone in ordered if float(frame).is_integer() else []:
            angle = eval_source.pose.bones[bone.name].matrix.to_quaternion().rotation_difference(eval_game.pose.bones[bone.name].matrix.to_quaternion()).angle
            angle = math.degrees(min(angle, abs(2 * math.pi - angle)))
            worst_bones[bone.name] = max(worst_bones.get(bone.name, 0.0), angle)
            max_angle = max(max_angle, angle)
        keyed = float(frame).is_integer()
        for original, candidate in pairs:
            errors = np.linalg.norm(positions(original, depsgraph) - positions(candidate, depsgraph), axis=1)
            assert np.isfinite(errors).all(), original.name
            target = max_errors if keyed else between_errors
            target[original.name] = max(target[original.name], float(errors.max(initial=0)))
        samples += 1
    frame_set(1)
    first = {n: game.pose.bones[n].matrix.copy() for n in ("root", "DEF-foot.L", "DEF-foot.R", "DEF-hand.L", "DEF-hand.R", "DEF-spine.006")}
    frame_set(end)
    return_error = max((game.pose.bones[n].matrix.translation - first[n].translation).length for n in first)
    worst = max(max_errors.values())
    tolerance = {"vertex_m": 0.002, "between_keys_vertex_m": 0.005, "angle_deg": 0.25, "foot_m": 0.001, "return_m": 0.001}
    return {
        "clip": clip_name, "duration_seconds": clip["duration"], "frames": end, "samples": samples, "loop": bool(clip.get("loop")),
        "source_action": source_action, "game_action": game_action, "shape_actions": shape_actions,
        "fps": clip.get("fps", 30),
        "max_vertex_error_metres": max_errors, "max_between_keys_vertex_error_metres": between_errors,
        "max_bone_angle_error_degrees": max_angle,
        "worst_angle_bones": sorted(worst_bones.items(), key=lambda kv: -kv[1])[:6],
        "root_foot_drift_metres": foot_drift, "neutral_return_error_metres": return_error,
        "corrective": corrective,
        "passed": worst <= tolerance["vertex_m"] and max(between_errors.values()) <= tolerance["between_keys_vertex_m"]
                  and max_angle <= tolerance["angle_deg"]
                  and foot_drift <= tolerance["foot_m"] and return_error <= tolerance["return_m"],
        "tolerance": tolerance, "exported": False}


selected_clips = [name for name in CLIPS if not ONLY or name in ONLY]
LEG_PROPS = [("thigh_parent.L", "IK_Stretch"), ("thigh_parent.R", "IK_Stretch")]
saved_leg_props = {bone: source.pose.bones[bone][prop] for bone, prop in LEG_PROPS}
for bone, prop in LEG_PROPS:
    source.pose.bones[bone][prop] = 0.0
bpy.context.view_layer.update()
saved_actions = {"source": source.animation_data.action, "game": game.animation_data.action if game.animation_data else None}

if PHASE == "preview":
    neutral = neutral_state()
    outputs = {}
    for name in selected_clips:
        clip = CLIPS[name]
        if clip.get("loop"):
            for t in (0.0, 4.0):
                set_clip_frame(name, clip, t, neutral, {})
                outputs[f"{name}_{t:.1f}"] = render_views(f"preview_{name}_{t:.1f}", VIEWS)
        else:
            arm_peaks, solves = peak_arms(clip, neutral)
            peak_time = clip["timing"]["in"] + 0.45
            set_clip_frame(name, clip, peak_time, neutral, arm_peaks)
            outputs[name] = {"renders": render_views(f"preview_{name}", VIEWS), "solves": solves}
        neutral_state()
    source.animation_data.action = saved_actions["source"]
    for bone, prop in LEG_PROPS:
        source.pose.bones[bone][prop] = saved_leg_props[bone]
    frame_set(1)
    result = {"phase": PHASE, "outputs": outputs}

elif PHASE == "author":
    backup = EVIDENCE / "Gratia_before_anime_clips.blend"
    if not backup.exists():
        assert "FINISHED" in bpy.ops.wm.save_as_mainfile(filepath=str(backup), copy=True, check_existing=False)
    previous = json.loads(report_path.read_text(encoding="utf-8")) if report_path.exists() else {"clips": []}
    reports = {c["clip"]: c for c in previous.get("clips", [])}
    for clip_name in selected_clips:
        clip = CLIPS[clip_name]
        for action in list(bpy.data.actions):
            if action.name.startswith(("Anime_Source_" + clip_name, "Anime_Game_" + clip_name, "Anime_" + clip_name + "_Shapes_")):
                bpy.data.actions.remove(action)
        neutral = neutral_state()
        arm_peaks = {} if clip.get("loop") else peak_arms(clip, neutral)[0]
        source_action = bpy.data.actions.new("Anime_Source_" + clip_name)
        game_action = bpy.data.actions.new("Anime_Game_" + clip_name)
        # Keep unassigned actions in the saved file for later export.
        source_action.use_fake_user = game_action.use_fake_user = True
        source.animation_data.action = source_action
        game.animation_data.action = game_action
        shape_actions = {}
        for original, candidate in pairs:
            for obj in (original, candidate):
                if not obj.data.shape_keys:
                    continue
                obj.data.shape_keys.animation_data_create()
                action = bpy.data.actions.new("Anime_" + clip_name + "_Shapes_" + obj.name)
                action.use_fake_user = True
                obj.data.shape_keys.animation_data.action = action
                shape_actions[obj.name] = action.name
        fps = use_fps(clip)
        end = int(round(clip["duration"] * fps)) + 1
        scene.frame_start, scene.frame_end = 1, end
        last_quat = {}
        for frame in range(1, end + 1):
            t = (frame - 1) / fps
            face = set_clip_frame(clip_name, clip, t, neutral, arm_peaks)
            for name in CONTROLS:
                bone = source.pose.bones[name]
                bone.keyframe_insert("rotation_quaternion", frame=frame, group=name)
                if name in FINGERS:
                    bone.keyframe_insert("scale", frame=frame, group=name)
            source.pose.bones["torso"].keyframe_insert("location", frame=frame, group="torso")
            for original, candidate in pairs:
                for obj in (original, candidate):
                    if not obj.data.shape_keys:
                        continue
                    for key in obj.data.shape_keys.key_blocks:
                        if key.name == "Basis":
                            continue
                        key.value = (1.0 if key.name == "Game_NeutralCorrective" else 0.0) if key.name.startswith("Game_") else face.get(key.name, 0.0)
                        key.keyframe_insert("value", frame=frame)
            depsgraph = frame_set(frame)
            evaluated = source.evaluated_get(depsgraph)
            transform = game.matrix_world.inverted() @ source.matrix_world
            targets = {bone.name: transform @ evaluated.pose.bones[bone.name].matrix.copy() for bone in ordered}
            for bone in ordered:
                kwargs = {"parent_matrix": targets[bone.parent.name], "parent_matrix_local": bone.parent.bone.matrix_local} if bone.parent else {}
                basis = bone.bone.convert_local_to_pose(targets[bone.name], bone.bone.matrix_local, invert=True, **kwargs)
                position, rotation, scale = basis.decompose()
                if bone.name in last_quat and rotation.dot(last_quat[bone.name]) < 0:
                    rotation.negate()
                last_quat[bone.name] = rotation.copy()
                bone.location, bone.rotation_quaternion, bone.scale = position, rotation, scale
                for channel in ("location", "rotation_quaternion", "scale"):
                    bone.keyframe_insert(channel, frame=frame, group=bone.name)
        linear(source_action)
        linear(game_action)
        for name in shape_actions.values():
            linear(bpy.data.actions[name])
        reports[clip_name] = validate(clip_name, clip, end, source_action.name, game_action.name, shape_actions)
        report_path.write_text(json.dumps({"phase": PHASE, "clips": list(reports.values())}, indent=2), encoding="utf-8")
    source.animation_data.action = saved_actions["source"]
    game.animation_data.action = saved_actions["game"]
    scene.render.fps, scene.render.fps_base = 30, 1
    frame_set(1)
    for bone, prop in LEG_PROPS:
        source.pose.bones[bone][prop] = saved_leg_props[bone]
    assert "FINISHED" in bpy.ops.wm.save_as_mainfile(filepath=str(ROOT / "Exports/Gratia/Gratia_mvp.blend"), check_existing=False)
    result = {"phase": PHASE, "clips": {k: {"passed": v["passed"], "worst_mm": max(v["max_vertex_error_metres"].values()) * 1000,
                                            "angle": v["max_bone_angle_error_degrees"], "drift_mm": v["root_foot_drift_metres"] * 1000,
                                            "return_mm": v["neutral_return_error_metres"] * 1000} for k, v in reports.items()}}

elif PHASE == "export":
    data = json.loads(report_path.read_text(encoding="utf-8"))
    exported = []
    selected = [game] + [candidate for _, candidate in pairs]
    for clip in data["clips"]:
        if ONLY and clip["clip"] not in ONLY:
            continue
        # Accepted exception: only the bone-angle limit fails, on dangling decor bones, with vertices in tolerance.
        tolerance = clip["tolerance"]
        decor_only = (not clip["passed"]
                      and max(clip["max_vertex_error_metres"].values()) <= tolerance["vertex_m"]
                      and max(clip["max_between_keys_vertex_error_metres"].values()) <= tolerance["between_keys_vertex_m"]
                      and clip["root_foot_drift_metres"] <= tolerance["foot_m"] and clip["neutral_return_error_metres"] <= tolerance["return_m"]
                      and all("decor" in bone or angle <= tolerance["angle_deg"] for bone, angle in clip["worst_angle_bones"])
                      and clip["worst_angle_bones"][-1][1] <= tolerance["angle_deg"]
                      and clip["max_bone_angle_error_degrees"] <= 0.5)
        assert clip["passed"] or decor_only, ("Stop before export: clip failed validation", clip["clip"])
        clip["accepted_exception"] = "decor bone angle <= 0.5 deg, vertices within tolerance" if decor_only else None
        use_fps(CLIPS[clip["clip"]])
        game.animation_data.action = bpy.data.actions[clip["game_action"]]
        for original, candidate in pairs:
            for obj in (original, candidate):
                if obj.name in clip["shape_actions"]:
                    obj.data.shape_keys.animation_data.action = bpy.data.actions[clip["shape_actions"][obj.name]]
        scene.frame_start, scene.frame_end = 1, clip["frames"]
        frame_set(1)
        for obj in scene.objects:
            obj.select_set(False)
        for obj in selected:
            obj.select_set(True)
        bpy.context.view_layer.objects.active = game
        path = OUT / ("Gratia_Game_" + clip["clip"] + ".fbx")
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
            clip.update(exported=True, fbx=str(path), bytes=path.stat().st_size, sha256=hashlib.file_digest(stream, "sha256").hexdigest())
        exported.append(str(path))
    game.animation_data.action = saved_actions["game"]
    source.animation_data.action = saved_actions["source"]
    frame_set(1)
    data["phase"] = PHASE
    report_path.write_text(json.dumps(data, indent=2), encoding="utf-8")
    for bone, prop in LEG_PROPS:
        source.pose.bones[bone][prop] = saved_leg_props[bone]
    assert "FINISHED" in bpy.ops.wm.save_as_mainfile(filepath=str(ROOT / "Exports/Gratia/Gratia_mvp.blend"), check_existing=False)
    result = {"phase": PHASE, "exported": exported}
elif PHASE == "correct":
    # Source skin keeps DQS/B-Bones; the candidate is LBS as in Unreal. Rest-space deltas
    # (inverse weighted LBS) of every frame are compressed by PCA into signed shapes.
    PREFIX = "Game_AnimeRef_"
    MAX_RANK = globals().get("MAX_RANK", 16)
    TARGET = 0.0008
    data = json.loads(report_path.read_text(encoding="utf-8"))
    entries = [c for c in data["clips"] if c["clip"] in CLIPS and (not ONLY or c["clip"] in ONLY)]
    for _, candidate in pairs:
        for key in list(candidate.data.shape_keys.key_blocks):
            if key.name.startswith(PREFIX):
                candidate.shape_key_remove(key)
    for entry in entries:
        for action_name in entry["shape_actions"].values():
            for bag in [b for layer in bpy.data.actions[action_name].layers for strip in layer.strips for b in strip.channelbags]:
                for curve in list(bag.fcurves):
                    if PREFIX in curve.data_path:
                        bag.fcurves.remove(curve)
    weighted = {}
    for _, candidate in pairs:
        relative = np.array(game.matrix_world.inverted() @ candidate.matrix_world)[:3, :3]
        assert np.allclose(relative, np.eye(3), atol=1e-5), candidate.name
        groups = {g.index: g.name for g in candidate.vertex_groups}
        vi, names, ws = [], [], []
        for vertex in candidate.data.vertices:
            for element in vertex.groups:
                name = groups[element.group]
                if name in game.data.bones and element.weight > 1e-8:
                    vi.append(vertex.index); names.append(name); ws.append(element.weight)
        weighted[candidate.name] = (np.array(vi), names, np.array(ws))
    to_armature = np.array(game.matrix_world.inverted(), dtype=np.float64)[:3, :3]

    def rest_delta(candidate, difference_world, depsgraph):
        rig = game.evaluated_get(depsgraph)
        vi, names, ws = weighted[candidate.name]
        count = len(candidate.data.vertices)
        skin = {n: np.array(rig.pose.bones[n].matrix @ game.data.bones[n].matrix_local.inverted(), dtype=np.float64)[:3, :3] for n in set(names)}
        matrices = np.zeros((count, 3, 3)); total = np.zeros(count)
        np.add.at(matrices, vi, np.array([skin[n] for n in names]) * ws[:, None, None])
        np.add.at(total, vi, ws)
        mask = total > 1e-8
        matrices[mask] /= total[mask, None, None]
        matrices[~mask] = np.eye(3)
        return np.linalg.solve(matrices, (difference_world @ to_armature.T)[..., None])[..., 0]

    originals = {c.name: o for o, c in pairs}
    active = {c.name: np.zeros(len(c.data.vertices), dtype=bool) for _, c in pairs}
    for entry in entries:
        use_fps(CLIPS[entry["clip"]])
        assign(entry["source_action"], entry["game_action"], entry["shape_actions"])
        for frame in range(1, entry["frames"] + 1, 2):
            depsgraph = frame_set(frame)
            for original, candidate in pairs:
                active[candidate.name] |= np.linalg.norm(positions(original, depsgraph) - positions(candidate, depsgraph), axis=1) > 0.0003
    layout = [(candidate, np.nonzero(active[candidate.name])[0]) for _, candidate in pairs if active[candidate.name].any()]
    rows, frames = [], []
    for index, entry in enumerate(entries):
        use_fps(CLIPS[entry["clip"]])
        assign(entry["source_action"], entry["game_action"], entry["shape_actions"])
        for frame in range(1, entry["frames"] + 1):
            depsgraph = frame_set(frame)
            parts = []
            for candidate, idx in layout:
                difference = positions(originals[candidate.name], depsgraph) - positions(candidate, depsgraph)
                parts.append(rest_delta(candidate, difference, depsgraph)[idx].astype(np.float32).ravel())
            rows.append(np.concatenate(parts))
            frames.append((index, frame))
    rows = np.stack(rows).astype(np.float64)
    bases = {}
    for index, entry in enumerate(entries):
        selection = [row for row, (owner, _) in enumerate(frames) if owner == index]
        D = rows[selection]
        values, vectors = np.linalg.eigh(D @ D.T)
        vectors = vectors[:, values.argsort()[::-1]]
        chosen, residual = MAX_RANK, None
        for rank in range(1, MAX_RANK + 1):
            U = vectors[:, :rank]
            rest = D - U @ (U.T @ D)
            residual = float(np.linalg.norm(rest.reshape(len(selection), -1, 3), axis=2).max())
            if residual <= TARGET:
                chosen = rank
                break
        U = vectors[:, :chosen]
        basis = D.T @ U
        scales = np.array([max(1e-9, float(np.linalg.norm(basis[:, k].reshape(-1, 3), axis=1).max())) for k in range(chosen)])
        bases[entry["clip"]] = {"basis": basis / scales, "coefficients": U * scales, "rank": chosen,
                                "residual_mm": residual * 1000, "rows": selection}
    for clip_name, item in bases.items():
        offset = 0
        for candidate, idx in layout:
            block = item["basis"][offset:offset + 3 * len(idx)].reshape(len(idx), 3, item["rank"])
            offset += 3 * len(idx)
            base = np.empty(len(candidate.data.vertices) * 3, dtype=np.float32)
            candidate.data.shape_keys.key_blocks["Basis"].data.foreach_get("co", base)
            base = base.reshape(-1, 3)
            for k in range(item["rank"]):
                shape = base.copy()
                shape[idx] += block[:, :, k]
                key = candidate.shape_key_add(name=f"{PREFIX}{clip_name}_{k + 1}", from_mix=False)
                key.slider_min, key.slider_max = -10.0, 10.0
                key.data.foreach_set("co", shape.astype(np.float32).ravel())
                key.value = 0.0
    for index, entry in enumerate(entries):
        item = bases[entry["clip"]]
        for local, row in enumerate(item["rows"]):
            frame = frames[row][1]
            for candidate, _ in layout:
                action = entry["shape_actions"].get(candidate.name)
                if not action:
                    continue
                candidate.data.shape_keys.animation_data.action = bpy.data.actions[action]
                for key in candidate.data.shape_keys.key_blocks:
                    if key.name.startswith(PREFIX):
                        prefix = f"{PREFIX}{entry['clip']}_"
                        key.value = float(item["coefficients"][local, int(key.name[len(prefix):]) - 1]) if key.name.startswith(prefix) else 0.0
                        key.keyframe_insert("value", frame=frame)
    for entry in entries:
        for action_name in entry["shape_actions"].values():
            linear(bpy.data.actions[action_name])
    corrective = {"method": "Rest-space inverse weighted LBS deltas per frame, per-clip PCA without centering",
                  "target_mm": TARGET * 1000, "frames": len(frames),
                  "active_vertices": {c.name: int(len(i)) for c, i in layout},
                  "clips": {k: {"rank": v["rank"], "residual_max_mm": v["residual_mm"],
                                "coefficient_range": [float(v["coefficients"].min()), float(v["coefficients"].max())]}
                            for k, v in bases.items()}}
    reports = {c["clip"]: c for c in data["clips"]}
    for entry in entries:
        reports[entry["clip"]] = validate(entry["clip"], CLIPS[entry["clip"]], entry["frames"], entry["source_action"],
                                          entry["game_action"], entry["shape_actions"], corrective["clips"][entry["clip"]])
    report_path.write_text(json.dumps({"phase": PHASE, "corrective": corrective, "clips": list(reports.values())}, indent=2), encoding="utf-8")
    source.animation_data.action = saved_actions["source"]
    game.animation_data.action = saved_actions["game"]
    scene.render.fps, scene.render.fps_base = 30, 1
    frame_set(1)
    for bone, prop in LEG_PROPS:
        source.pose.bones[bone][prop] = saved_leg_props[bone]
    assert "FINISHED" in bpy.ops.wm.save_as_mainfile(filepath=str(ROOT / "Exports/Gratia/Gratia_mvp.blend"), check_existing=False)
    result = {"phase": PHASE, "corrective": corrective,
              "clips": {k: {"passed": v["passed"], "worst_mm": max(v["max_vertex_error_metres"].values()) * 1000,
                            "drift_mm": v["root_foot_drift_metres"] * 1000} for k, v in reports.items() if k in CLIPS}}
else:
    raise RuntimeError("Unknown PHASE: " + str(PHASE))
