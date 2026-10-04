"""Original reference-informed reactions, executed only through Blender MCP.

PHASE is supplied by the MCP request: "author" prepares separate actions and
measures meshes; "export" exports only previously validated clips. No existing
Soft/Bright action, source file or mesh corrective is replaced.
"""
import bpy
import hashlib
import json
import math
from pathlib import Path

import numpy as np
from mathutils import Matrix, Quaternion, Vector

ROOT = Path("E:/coding/ue proto")
OUT = ROOT / "Exports/Gratia/GameRig"
EVIDENCE = ROOT / "evidence/04/blender_mcp"
EVIDENCE.mkdir(parents=True, exist_ok=True)
PHASE = globals().get("PHASE", "author")
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
manifest = json.loads((OUT / "game_rig_manifest.json").read_text(encoding="utf-8"))
pairs = [(bpy.data.objects[item["source"]], bpy.data.objects[item["candidate"]]) for item in manifest["meshes"]]
report_path = EVIDENCE / "reference_reactions_validation.json"
controls = ["upper_arm_fk.L", "upper_arm_fk.R", "forearm_fk.L", "forearm_fk.R",
            "hand_fk.L", "hand_fk.R", "spine_fk.003", "head"]
finger_controls = [bone.name for bone in source.pose.bones if "_master." in bone.name]
controls += finger_controls


def curves(action):
    return [curve for layer in action.layers for strip in layer.strips
            for bag in strip.channelbags for curve in bag.fcurves]


def linear(action):
    for curve in curves(action):
        for key in curve.keyframe_points:
            key.interpolation = "LINEAR"


def frame_set(frame):
    scene.frame_set(math.floor(frame), subframe=frame % 1)
    bpy.context.view_layer.update()
    return bpy.context.evaluated_depsgraph_get()


def positions(obj, depsgraph):
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    coords = np.empty(len(mesh.vertices) * 3, dtype=np.float32)
    mesh.vertices.foreach_get("co", coords)
    world = np.array(evaluated.matrix_world, dtype=np.float64)
    evaluated.to_mesh_clear()
    return coords.reshape((-1, 3)).astype(np.float64) @ world[:3, :3].T + world[:3, 3]


def depth(bone):
    return 0 if not bone.parent else depth(bone.parent) + 1


ordered = sorted(game.pose.bones, key=lambda bone: depth(bone.bone))


def envelope(time, rise, hold_end, duration):
    if time <= 0 or time >= duration:
        return 0.0
    v = min(1.0, time / rise) if time < rise else 1.0 if time <= hold_end else (duration - time) / (duration - hold_end)
    return v * v * (3.0 - 2.0 * v)


def world_rotation(name, axis, degrees):
    local_axis = source.data.bones[name].matrix_local.to_quaternion().inverted() @ Vector(axis)
    return Quaternion(local_axis, math.radians(degrees))


def solve_arm(side, target, pole):
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
    for control, begin, end in [(upper, shoulder, elbow), (lower, elbow, Vector(target))]:
        rest = control.bone.matrix_local
        direction = (control.bone.tail_local - control.bone.head_local).normalized()
        rotation = direction.rotation_difference((end - begin).normalized()) @ rest.to_quaternion()
        control.matrix = Matrix.LocRotScale(begin, rotation, Vector((1, 1, 1)))
        bpy.context.view_layer.update()
    # Keep the palm upright and softly facing the upper torso instead of
    # inheriting an awkward straight wrist from the rest pose.
    rest_wrist = wrist.bone.matrix_local.to_quaternion()
    rest_axis = (wrist.bone.tail_local - wrist.bone.head_local).normalized()
    wanted_axis = Vector((0.12 if side == "L" else -0.12, -0.25, 0.96)).normalized()
    # Transport the forearm's roll to the wrist before aligning the fingers.
    # Aligning from the world rest wrist introduced a 180-degree Rigify twist
    # branch crossing between frames 19 and 20 (77 degrees of half-frame error).
    inherited = lower.matrix.to_quaternion() @ lower.bone.matrix_local.to_quaternion().inverted() @ rest_wrist
    inherited_axis = inherited @ (rest_wrist.inverted() @ rest_axis)
    palm_rotation = inherited_axis.rotation_difference(wanted_axis) @ inherited
    wrist.matrix = Matrix.LocRotScale(Vector(target), palm_rotation, Vector((1, 1, 1)))
    bpy.context.view_layer.update()
    return {"target": list(target), "elbow": list(elbow),
            "actual_hand": list(source.evaluated_get(bpy.context.evaluated_depsgraph_get()).pose.bones["DEF-hand." + side].matrix.translation)}


if PHASE == "author":
    backup = EVIDENCE / "Gratia_before_reference_refinement.blend"
    if not backup.exists():
        assert "FINISHED" in bpy.ops.wm.save_as_mainfile(filepath=str(backup), copy=True, check_existing=False)
    source_actions = json.loads(game["mvp_source_actions"])
    source.animation_data.action = bpy.data.actions[source_actions["TestHead"]]
    frame_set(1)
    # TestHead does not key wrists/fingers. Do not inherit their last evaluated
    # values from a previous gesture when repeating the preparation pipeline.
    for name in ["hand_fk.L", "hand_fk.R"] + finger_controls:
        source.pose.bones[name].rotation_quaternion = Quaternion((1, 0, 0, 0))
        source.pose.bones[name].scale = Vector((1, 1, 1))
    bpy.context.view_layer.update()
    # Reset only this pipeline's optional bases before authoring fresh paths.
    for _, candidate in pairs:
        for key in list(candidate.data.shape_keys.key_blocks):
            if not key.name.startswith(("Game_HandRef_", "Game_CheerRef_")): continue
            path = key.path_from_id("value")
            for action in bpy.data.actions:
                for layer in action.layers:
                    for strip in layer.strips:
                        for bag in strip.channelbags:
                            for curve in list(bag.fcurves):
                                if curve.data_path == path: bag.fcurves.remove(curve)
            candidate.shape_key_remove(key)
    neutral = {name: source.pose.bones[name].rotation_quaternion.copy() for name in controls}
    source.animation_data.action = None
    for name in controls:
        source.pose.bones[name].rotation_mode = "QUATERNION"
        source.pose.bones[name].rotation_quaternion = neutral[name]
    bpy.context.view_layer.update()
    hand_solves = []
    neutral_scales = {name: source.pose.bones[name].scale.copy() for name in finger_controls}
    neutral_all = {bone.name: (bone.location.copy(), bone.rotation_quaternion.copy(), bone.scale.copy())
                   for bone in source.pose.bones}

    def reset_controls():
        for name in controls:
            source.pose.bones[name].rotation_quaternion = neutral[name]
            if name in neutral_scales: source.pose.bones[name].scale = neutral_scales[name]
        bpy.context.view_layer.update()

    peaks = {}
    reset_controls()
    hand_solves.append(solve_arm("R", (-0.13, -0.175, 1.405), (-0.39, -0.01, 1.285)))
    for name in finger_controls:
        if name.endswith(".R"):
            source.pose.bones[name].rotation_quaternion = neutral[name] @ Quaternion((1, 0, 0), math.radians(8 if name.startswith("thumb") else 15))
            source.pose.bones[name].scale.y = 0.94 if name.startswith("thumb") else 0.9
    peak_scales = {"ReactHand": {name: source.pose.bones[name].scale.copy() for name in finger_controls}}
    peaks["ReactHand"] = {name: source.pose.bones[name].rotation_quaternion.copy() for name in controls}
    reset_controls()
    for side, sign in (("L", 1), ("R", -1)):
        hand_solves.append(solve_arm(side, (sign * 0.17, -0.195, 1.45), (sign * 0.39, -0.02, 1.28)))
    for name in finger_controls:
        # A deliberately loose curl. This is an authored gesture, not a
        # claim that the source video's clenched fingers were recovered.
        source.pose.bones[name].rotation_quaternion = neutral[name] @ Quaternion((1, 0, 0), math.radians(18 if name.startswith("thumb") else 25))
        source.pose.bones[name].scale.y = 0.92 if name.startswith("thumb") else 0.76
    peak_scales["ReactCheer"] = {name: source.pose.bones[name].scale.copy() for name in finger_controls}
    peaks["ReactCheer"] = {name: source.pose.bones[name].rotation_quaternion.copy() for name in controls}
    reset_controls()
    peaks["ReactFace"] = dict(neutral)
    peak_scales["ReactFace"] = dict(neutral_scales)

    reports = []
    for clip, duration, rise, hold_end in [("ReactFace", 2.0, 0.45, 0.95),
                                         ("ReactHand", 2.4, 0.65, 1.2),
                                         ("ReactCheer", 2.4, 0.7, 1.3)]:
        # Separate action names retain the old authored/exported Soft/Bright resources.
        source_action = bpy.data.actions.new("Reference_Source_" + clip)
        game_action = bpy.data.actions.new("Reference_Game_" + clip)
        source.animation_data.action = source_action
        game.animation_data.action = game_action
        corrective_actions = {}
        source_shape_actions = {}
        for original, candidate in pairs:
            candidate.data.shape_keys.animation_data_create()
            corrective = bpy.data.actions.new("Reference_" + clip + "_Shapes_" + candidate.name)
            candidate.data.shape_keys.animation_data.action = corrective
            corrective_actions[candidate.name] = corrective.name
            if original.data.shape_keys:
                original.data.shape_keys.animation_data_create()
                original.data.shape_keys.animation_data.action = bpy.data.actions.new("Reference_" + clip + "_SourceShapes_" + original.name)
                source_shape_actions[original.name] = original.data.shape_keys.animation_data.action.name
        end = int(round(duration * 30)) + 1
        scene.frame_start, scene.frame_end = 1, end
        last_quat = {}
        for frame in range(1, end + 1):
            time = (frame - 1) / 30
            amount = envelope(time, rise, hold_end, duration)
            for name in controls:
                delayed = envelope(max(0, time - 0.08), rise, hold_end, duration - 0.08) if clip == "ReactCheer" and name.endswith(".L") else amount
                source.pose.bones[name].rotation_quaternion = neutral[name].slerp(peaks[clip][name], delayed)
                if name in neutral_scales: source.pose.bones[name].scale = neutral_scales[name].lerp(peak_scales[clip][name], delayed)
            # Face keeps the validated gentle head pathway; hand/cheer use
            # a small accompanying original head inclination and chest motion.
            yaw = (6.3 if clip == "ReactFace" else -5 if clip == "ReactHand" else 2) * amount
            tilt = (1.4 if clip == "ReactFace" else 4 if clip == "ReactHand" else 5) * amount
            source.pose.bones["head"].rotation_quaternion = world_rotation("head", (0, 0, 1), yaw) @ world_rotation("head", (1, 0, 0), tilt)
            source.pose.bones["spine_fk.003"].rotation_quaternion = world_rotation("spine_fk.003", (1, 0, 0), 0.7 * amount)
            for name in controls:
                source.pose.bones[name].keyframe_insert("rotation_quaternion", frame=frame, group=name)
                if name in neutral_scales: source.pose.bones[name].keyframe_insert("scale", frame=frame, group=name)
            # Combine authored facial beat with the exact same shape values on
            # source/candidate. Candidate Game_ shapes remain the old nine.
            blink = envelope(time - 0.5, 0.065, 0.10, 0.23) if clip != "ReactHand" else envelope(time - 0.85, 0.065, 0.10, 0.23)
            face_values = {"Eye L close": blink, "Eye R close": blink,
                           "Mouth smile": amount * (0.42 if clip == "ReactCheer" else 0.16),
                           "Brows up": amount * 0.10}
            corrective_values = {"Game_NeutralCorrective": 1.0}
            if clip == "ReactFace":
                progress = min(1.0, 0.35 * amount)
                lower = min(3, int(progress * 4))
                fraction = progress * 4 - lower
                names = ["Game_NeutralCorrective"] + ["Game_HeadCorrective_" + str(i) for i in range(1, 5)]
                corrective_values = {names[lower]: 1.0 - fraction, names[lower + 1]: fraction}
            for original, candidate in pairs:
                for obj in (original, candidate):
                    if not obj.data.shape_keys:
                        continue
                    for key in obj.data.shape_keys.key_blocks:
                        if key.name.startswith("Game_"):
                            # Neutral basis is tested honestly for the new arm paths.
                            key.value = corrective_values.get(key.name, 0.0)
                        elif key.name != "Basis":
                            key.value = face_values.get(key.name, 0.0)
                        if key.name != "Basis":
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
        linear(source_action); linear(game_action)
        for original, candidate in pairs:
            linear(candidate.data.shape_keys.animation_data.action)
            if original.data.shape_keys: linear(original.data.shape_keys.animation_data.action)
        max_errors = {original.name: 0.0 for original, candidate in pairs}
        max_angle = 0.0
        foot_reference = None
        foot_drift = 0.0
        samples = 0
        for frame in np.arange(1, end + 0.1, 0.5):
            depsgraph = frame_set(float(frame))
            eval_source = source.evaluated_get(depsgraph)
            eval_game = game.evaluated_get(depsgraph)
            feet = {name: eval_game.pose.bones[name].matrix.translation.copy() for name in ("root", "DEF-foot.L", "DEF-foot.R")}
            if foot_reference is None: foot_reference = feet
            foot_drift = max(foot_drift, max((feet[name] - foot_reference[name]).length for name in feet))
            for bone in ordered:
                angle = eval_source.pose.bones[bone.name].matrix.to_quaternion().rotation_difference(eval_game.pose.bones[bone.name].matrix.to_quaternion()).angle
                max_angle = max(max_angle, math.degrees(min(angle, abs(2 * math.pi - angle))))
            for original, candidate in pairs:
                errors = np.linalg.norm(positions(original, depsgraph) - positions(candidate, depsgraph), axis=1)
                assert np.isfinite(errors).all(), original.name
                max_errors[original.name] = max(max_errors[original.name], float(errors.max(initial=0)))
            samples += 1
        frame_set(1)
        first = {name: game.pose.bones[name].matrix.copy() for name in ("root", "DEF-foot.L", "DEF-foot.R", "DEF-hand.L", "DEF-hand.R", "DEF-spine.006")}
        frame_set(end)
        return_error = max((game.pose.bones[name].matrix.translation - first[name].translation).length for name in first)
        frame_set((end + 1) / 2)
        midpoint = {name: list(game.pose.bones[name].matrix.translation) for name in ("DEF-hand.L", "DEF-hand.R", "DEF-spine.006")}
        reports.append({"clip": clip, "duration_seconds": duration, "frames": end, "samples": samples,
                        "source_action": source_action.name, "game_action": game_action.name,
                        "corrective_actions": corrective_actions,
                        "source_shape_actions": source_shape_actions,
                        "max_vertex_error_metres": max_errors, "max_bone_angle_error_degrees": max_angle,
                        "root_foot_drift_metres": foot_drift, "neutral_return_error_metres": return_error,
                        "midpoint_positions_metres": midpoint,
                        "passed": max(max_errors.values()) <= 0.001 and max_angle <= 0.1 and foot_drift <= 0.001 and return_error <= 0.001,
                        "exported": False})
        report_path.write_text(json.dumps({"phase": PHASE, "hand_pose_solves": hand_solves, "clips": reports}, indent=2), encoding="utf-8")
    frame_set(1)
    assert "FINISHED" in bpy.ops.wm.save_as_mainfile(filepath=str(ROOT / "Exports/Gratia/Gratia_mvp.blend"), check_existing=False)
    result = {"phase": PHASE, "backup": str(backup), "report": str(report_path), "clips": reports}
elif PHASE == "export":
    data = json.loads(report_path.read_text(encoding="utf-8"))
    assert all(clip["passed"] for clip in data["clips"]), "Every final reaction must pass before any export"
    assert len(data.get("old_clip_regression", [])) == 5 and all(clip["passed"] for clip in data["old_clip_regression"]), "Run corrective/regression validation before export"
    exported = []
    for clip in data["clips"]:
        assert clip["passed"], ("Stop before export: candidate exceeds validated deformation tolerance", clip["clip"], clip["max_vertex_error_metres"])
        source.animation_data.action = bpy.data.actions[clip["source_action"]]
        game.animation_data.action = bpy.data.actions[clip["game_action"]]
        for original, candidate in pairs:
            candidate.data.shape_keys.animation_data.action = bpy.data.actions[clip["corrective_actions"][candidate.name]]
        scene.frame_start, scene.frame_end = 1, clip["frames"]
        frame_set(1)
        for obj in scene.objects: obj.select_set(False)
        selected = [game] + [candidate for original, candidate in pairs]
        for obj in selected: obj.select_set(True)
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
        clip.update(exported=True, fbx=str(path))
        exported.append(str(path))
    # Export a separate mesh so Unreal can import it only after its integration
    # barrier. Existing approved preview/Soft/Bright exports stay untouched.
    game.animation_data.action = bpy.data.actions[json.loads(game["mvp_game_actions"])["Idle"]]
    source.animation_data.action = bpy.data.actions[json.loads(game["mvp_source_actions"])["Idle"]]
    for original, candidate in pairs:
        candidate.data.shape_keys.animation_data.action = bpy.data.actions[candidate["mvp_corrective_action_Idle"]]
        for key in candidate.data.shape_keys.key_blocks:
            if key.name != "Basis": key.value = 0.0
    frame_set(1)
    mesh_path = OUT / "Gratia_Game_reference_preview.fbx"
    with bpy.context.temp_override(active_object=game, object=game, selected_objects=selected, selected_editable_objects=selected):
        assert "FINISHED" in bpy.ops.export_scene.fbx(
            filepath=str(mesh_path), use_selection=True, object_types={"MESH", "ARMATURE"}, global_scale=1,
            apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS", axis_forward="-Y", axis_up="Z",
            use_mesh_modifiers=False, mesh_smooth_type="FACE", add_leaf_bones=False,
            use_armature_deform_only=True, armature_nodetype="NULL", path_mode="AUTO", embed_textures=False,
            bake_anim=False)
    data["mesh_fbx"] = str(mesh_path)
    data["mesh_parts"] = [{"source": original.name, "candidate": candidate.name,
                           "vertices": len(candidate.data.vertices),
                           "material_slots": [slot.material.name if slot.material else None for slot in candidate.material_slots],
                           "shape_names": [key.name for key in candidate.data.shape_keys.key_blocks]}
                          for original, candidate in pairs]
    data["declared_morph_names"] = sorted({key.name for _, candidate in pairs for key in candidate.data.shape_keys.key_blocks if key.name != "Basis"})
    data["declared_morph_count"] = len(data["declared_morph_names"])
    data["nonzero_morph_names"] = []
    for name in data["declared_morph_names"]:
        for _, candidate in pairs:
            key = candidate.data.shape_keys.key_blocks.get(name)
            if key is None: continue
            basis = candidate.data.shape_keys.key_blocks["Basis"]
            if any((point.co - base.co).length > 1e-8 for point, base in zip(key.data, basis.data)):
                data["nonzero_morph_names"].append(name)
                break
    data["nonzero_morph_count"] = len(data["nonzero_morph_names"])
    first_pass_path = EVIDENCE / "reference_reactions_first_pass.json"
    if first_pass_path.exists():
        first_pass = json.loads(first_pass_path.read_text(encoding="utf-8"))
        old_slots = {part["candidate"]: part["material_slots"] for part in first_pass.get("mesh_parts", [])}
        assert all(part["material_slots"] == old_slots.get(part["candidate"], part["material_slots"]) for part in data["mesh_parts"]), "Material slot order changed"
    data["exports"] = []
    for path in [mesh_path] + [Path(clip["fbx"]) for clip in data["clips"]]:
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        data["exports"].append({"path": str(path), "bytes": path.stat().st_size, "sha256": digest})
    report_path.write_text(json.dumps(data, indent=2), encoding="utf-8")
    assert "FINISHED" in bpy.ops.wm.save_as_mainfile(filepath=str(ROOT / "Exports/Gratia/Gratia_mvp.blend"), check_existing=False)
    result = {"phase": PHASE, "exported": exported, "mesh": str(mesh_path),
              "nonzero_morph_count": data["nonzero_morph_count"], "exports": data["exports"]}
else:
    raise RuntimeError("Unknown PHASE: " + str(PHASE))
