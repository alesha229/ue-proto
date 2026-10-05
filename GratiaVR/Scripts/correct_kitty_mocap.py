"""HISTORICAL (rejected KM466 v2 trial, superseded by author_vam_mocap.py); do not run.

Build/validate isolated KM466 trial correctives in the active Blender MCP.

Execute with PHASE = 'correct' (default), or PHASE = 'validate'. This source
must be supplied to execute_blender_code; it does not launch Blender. Only the
dedicated *_KM466_Trial meshes and their Game_KM466_* channels are authored.
The report gates saving: all integer and half frames must pass unchanged 1 mm
surface/bone-position and 0.1 degree bone-angle tolerances.
"""
import bpy
import hashlib
import json
import math
import time
from pathlib import Path

import numpy as np

ROOT = Path("E:/coding/ue proto")
WORKING = ROOT / "Exports/Gratia/Gratia_mvp.blend"
REPORT = ROOT / "evidence/05/kitty_mocap_v2/gratia_mocap_validation.json"
PREFIX = "Game_KM466_"
PHASE = globals().get("PHASE", "correct")
assert PHASE in ("correct", "validate"), PHASE
assert Path(bpy.data.filepath).resolve() == WORKING.resolve(), bpy.data.filepath
data = json.loads(REPORT.read_text(encoding="utf-8"))
assert data["frames"] == 301 and data["fps"] == 30 and abs(data.get("duration_seconds", data.get("duration", -1)) - 10) < 1e-6
source = bpy.data.objects[data.get("source_rig", "Gratia")]
game = bpy.data.objects[data.get("game_rig", "Gratia_GameRig")]
pair_records = data.get("pairs", data.get("meshes"))
assert pair_records, "Report must contain nonempty pairs or meshes"
pairs = [(bpy.data.objects[item["source"]], bpy.data.objects[item["candidate"]]) for item in pair_records]
assert len({b.name for _, b in pairs}) == len(pairs)
assert all(b.name.endswith("_KM466_Trial") and a != b and a.data != b.data for a, b in pairs)
assert all(b.data.shape_keys is not None for _, b in pairs)
assert all(a.data.shape_keys != b.data.shape_keys for a, b in pairs if a.data.shape_keys)
assert source.type == game.type == "ARMATURE"
source_action = bpy.data.actions[data["source_action"]]
game_action = bpy.data.actions[data["game_action"]]
shape_actions = {b.name: bpy.data.actions[data["shape_actions"][b.name]] for _, b in pairs}
previous_scene = bpy.context.window.scene
bpy.context.window.scene = bpy.data.scenes[data['scene']]
scene = bpy.context.scene
metres_per_unit = float(scene.unit_settings.scale_length) or 1.0
SURFACE_TOLERANCE = POSITION_TOLERANCE = 0.001
ANGLE_TOLERANCE = 0.1
MAX_RANK = 32
started = time.monotonic()


def curves(action):
    if action is None:
        return []
    if hasattr(action, "layers"):
        return [curve for layer in action.layers for strip in layer.strips
                for bag in strip.channelbags for curve in bag.fcurves]
    return list(action.fcurves)


def remove_curve(action, curve):
    if hasattr(action, "layers"):
        for layer in action.layers:
            for strip in layer.strips:
                for bag in strip.channelbags:
                    if curve in list(bag.fcurves):
                        bag.fcurves.remove(curve)
                        return
    else:
        action.fcurves.remove(curve)


def animation_state(owner):
    animation = owner.animation_data
    return {"existed": animation is not None,
            "action": animation.action if animation else None,
            "slot": animation.action_slot if animation and hasattr(animation, "action_slot") else None,
            "use_nla": animation.use_nla if animation else True}


def restore_animation(owner, saved):
    if not saved["existed"]:
        owner.animation_data_clear()
        return
    owner.animation_data_create()
    owner.animation_data.action = saved["action"]
    if saved["slot"] is not None and saved["action"] is not None:
        owner.animation_data.action_slot = saved["slot"]
    owner.animation_data.use_nla = saved["use_nla"]


def transforms(owner):
    values = {}
    for name in ("location", "rotation_mode", "rotation_euler", "rotation_quaternion", "rotation_axis_angle", "scale"):
        value = getattr(owner, name)
        values[name] = list(value) if name == "rotation_axis_angle" else value.copy() if hasattr(value, "copy") else value
    return values


def custom_properties(owner):
    # FK/IK and stretch controls can be written by source_action evaluation.
    # Plain scalar/array ID properties cover those controls without copying
    # Blender's internal _RNA_UI or replacing unrelated pointer properties.
    values = {}
    for name in owner.keys():
        if name == "_RNA_UI":
            continue
        value = owner[name]
        if isinstance(value, (str, int, float, bool)):
            values[name] = value
        elif hasattr(value, "to_list"):
            values[name] = value.to_list()
    return values


rig_state = {obj.name: {"animation": animation_state(obj), "transforms": transforms(obj),
                        "matrix_basis": obj.matrix_basis.copy(),
                        "properties": custom_properties(obj), "pose_position": obj.data.pose_position,
                        "bones": {bone.name: (transforms(bone), custom_properties(bone), bone.matrix_basis.copy()) for bone in obj.pose.bones}}
             for obj in (source, game)}
shape_owners = {obj.data.shape_keys.name: obj.data.shape_keys for pair in pairs for obj in pair if obj.data.shape_keys}
shape_state = {name: {"animation": animation_state(owner),
                      "values": {key.name: key.value for key in owner.key_blocks}}
               for name, owner in shape_owners.items()}
modifier_state = [(obj, modifier.name, modifier.show_viewport, modifier.show_render)
                  for pair in pairs for obj in pair for modifier in obj.modifiers]
visibility_state = {obj.name: obj.hide_get() for obj in [source, game] + [obj for pair in pairs for obj in pair]}
scene_state = {"frame": scene.frame_current, "subframe": scene.frame_subframe,
               "start": scene.frame_start, "end": scene.frame_end,
               "fps": scene.render.fps, "fps_base": scene.render.fps_base}
prior_names = {b.name: [key.name for key in b.data.shape_keys.key_blocks if not key.name.startswith(PREFIX)]
               for _, b in pairs}
source_shape_names = {a.name: [key.name for key in a.data.shape_keys.key_blocks] if a.data.shape_keys else [] for a, _ in pairs}
before_sha256 = hashlib.sha256(WORKING.read_bytes()).hexdigest()


def frame_set(frame):
    scene.frame_set(math.floor(frame), subframe=float(frame % 1))
    bpy.context.view_layer.update()
    return bpy.context.evaluated_depsgraph_get()


def set_action(owner, action):
    owner.animation_data_create()
    owner.animation_data.action = action
    if action is not None and hasattr(action, "slots") and len(action.slots) == 1:
        owner.animation_data.action_slot = action.slots[0]
    owner.animation_data.use_nla = False


def set_clip():
    set_action(source, source_action)
    set_action(game, game_action)
    source.data.pose_position = game.data.pose_position = "POSE"
    for name in visibility_state:
        bpy.data.objects[name].hide_set(False)
    for original, candidate in pairs:
        set_action(candidate.data.shape_keys, shape_actions[candidate.name])
        if original.data.shape_keys:
            # The source action also owns eye-closure curves; evaluate them
            # alongside the trial curves instead of freezing source values.
            set_action(original.data.shape_keys, bpy.data.actions[data["shape_actions"][original.name]])
        for modifier in original.modifiers:
            modifier.show_viewport = modifier.type == "ARMATURE"
        for modifier in candidate.modifiers:
            modifier.show_viewport = modifier.type == "ARMATURE"
        armature_modifiers = [m for m in candidate.modifiers if m.type == "ARMATURE"]
        assert len(armature_modifiers) == 1 and armature_modifiers[0].object == game, candidate.name
        armature = armature_modifiers[0]
        assert not armature.use_deform_preserve_volume, "Trial skin must already use LBS"
        assert armature.use_vertex_groups and not armature.use_bone_envelopes
        assert not armature.vertex_group and not armature.use_multi_modifier
    scene.frame_start, scene.frame_end = 1, data["frames"]
    scene.render.fps, scene.render.fps_base = data["fps"], 1.0


def positions(obj, depsgraph, world=False):
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    try:
        values = np.empty(len(mesh.vertices) * 3, dtype=np.float32)
        mesh.vertices.foreach_get("co", values)
        points = values.reshape((-1, 3)).astype(np.float64)
        if world:
            matrix = np.asarray(evaluated.matrix_world, dtype=np.float64)
            points = points @ matrix[:3, :3].T + matrix[:3, 3]
        assert np.isfinite(points).all(), obj.name
        return points
    finally:
        evaluated.to_mesh_clear()


bone_names = [bone.name for bone in game.data.bones]
assert all(name in source.data.bones for name in bone_names), "Missing source counterpart"
root_foot_names = data.get("root_foot_bones", ["root", "DEF-foot.L", "DEF-foot.R"])
assert all(name in game.data.bones and name in source.data.bones for name in root_foot_names)
weights, offsets = {}, {}
feature_count = 0
for original, candidate in pairs:
    assert len(original.data.vertices) == len(candidate.data.vertices), candidate.name
    vertex_indices, bone_indices, amounts = [], [], []
    deform_names = [bone.name for bone in game.data.bones if bone.use_deform]
    index_by_name = {name: index for index, name in enumerate(deform_names)}
    for vertex in candidate.data.vertices:
        for group in vertex.groups:
            name = candidate.vertex_groups[group.group].name
            if name in index_by_name and group.weight > 1e-8:
                vertex_indices.append(vertex.index)
                bone_indices.append(index_by_name[name])
                amounts.append(group.weight)
    vi = np.asarray(vertex_indices, dtype=np.int32)
    bi = np.asarray(bone_indices, dtype=np.int32)
    weight = np.asarray(amounts, dtype=np.float64)
    totals = np.bincount(vi, weights=weight, minlength=len(candidate.data.vertices))
    normalized = weight / np.maximum(totals[vi], 1e-12)
    weights[candidate.name] = (vi, bi, normalized, totals > 1e-8, deform_names)
    offsets[candidate.name] = slice(feature_count, feature_count + len(candidate.data.vertices) * 3)
    feature_count += len(candidate.data.vertices) * 3
rest_inverse = {name: game.data.bones[name].matrix_local.inverted() for name in deform_names}


def inverse_delta(candidate, difference_local, depsgraph):
    rig = game.evaluated_get(depsgraph)
    vi, bi, normalized, weighted, names = weights[candidate.name]
    skin = np.stack([np.asarray(rig.pose.bones[name].matrix @ rest_inverse[name], dtype=np.float64)[:3, :3]
                     for name in names])
    matrices = np.zeros((len(candidate.data.vertices), 3, 3), dtype=np.float64)
    np.add.at(matrices, vi, skin[bi] * normalized[:, None, None])
    matrices[~weighted] = np.eye(3)
    # An Armature modifier works in armature space, including differing object
    # transforms. Transform the derivative back to candidate mesh coordinates.
    mesh_to_rig = np.asarray(rig.matrix_world.inverted() @ candidate.evaluated_get(depsgraph).matrix_world,
                             dtype=np.float64)[:3, :3]
    matrices = np.linalg.inv(mesh_to_rig) @ matrices @ mesh_to_rig
    solved = np.linalg.solve(matrices, difference_local[..., None])[..., 0]
    assert np.isfinite(solved).all(), candidate.name
    return solved


def residual_vector(frame):
    depsgraph = frame_set(frame)
    vector = np.empty(feature_count, dtype=np.float32)
    for original, candidate in pairs:
        target_world = positions(original, depsgraph, True)
        current_world = positions(candidate, depsgraph, True)
        world_to_mesh = np.asarray(candidate.evaluated_get(depsgraph).matrix_world.inverted(), dtype=np.float64)[:3, :3]
        difference_local = (target_world - current_world) @ world_to_mesh.T
        vector[offsets[candidate.name]] = inverse_delta(candidate, difference_local, depsgraph).ravel()
    assert np.isfinite(vector).all()
    return vector


def remove_owned_shapes():
    for _, candidate in pairs:
        action = shape_actions[candidate.name]
        for key in list(candidate.data.shape_keys.key_blocks):
            if key.name.startswith(PREFIX):
                path = key.path_from_id("value")
                for curve in curves(action):
                    if curve.data_path == path:
                        remove_curve(action, curve)
                candidate.shape_key_remove(key)


def validate():
    errors_integer = {a.name: 0.0 for a, _ in pairs}
    errors_half = errors_integer.copy()
    worst = {}
    max_bone_angle = max_bone_position = max_root_foot = 0.0
    root_foot_errors = {name: 0.0 for name in root_foot_names}
    sample_errors = []
    for sample in range(data["frames"] * 2 - 1):
        frame = 1.0 + sample * 0.5
        depsgraph = frame_set(frame)
        original_rig, candidate_rig = source.evaluated_get(depsgraph), game.evaluated_get(depsgraph)
        for name in bone_names:
            original_matrix = original_rig.matrix_world @ original_rig.pose.bones[name].matrix
            candidate_matrix = candidate_rig.matrix_world @ candidate_rig.pose.bones[name].matrix
            distance = (original_matrix.translation - candidate_matrix.translation).length * metres_per_unit
            angle = original_matrix.to_quaternion().rotation_difference(candidate_matrix.to_quaternion()).angle
            angle = math.degrees(min(angle, abs(2 * math.pi - angle)))
            assert math.isfinite(distance) and math.isfinite(angle), (name, frame)
            max_bone_position = max(max_bone_position, distance)
            max_bone_angle = max(max_bone_angle, angle)
            if name in root_foot_errors:
                root_foot_errors[name] = max(root_foot_errors[name], distance)
                max_root_foot = max(max_root_foot, distance)
        current_maximum = 0.0
        for original, candidate in pairs:
            distances = np.linalg.norm(positions(original, depsgraph, True) - positions(candidate, depsgraph, True), axis=1) * metres_per_unit
            maximum = float(distances.max(initial=0))
            bucket = errors_integer if sample % 2 == 0 else errors_half
            bucket[original.name] = max(bucket[original.name], maximum)
            current_maximum = max(current_maximum, maximum)
            if maximum > worst.get(original.name, {}).get("error_metres", -1):
                worst[original.name] = {"frame": frame, "vertex": int(distances.argmax()), "error_metres": maximum,
                                        "candidate": candidate.name, "sample_type": "integer" if sample % 2 == 0 else "half"}
        sample_errors.append((current_maximum, frame))
    errors = {name: max(errors_integer[name], errors_half[name]) for name in errors_integer}
    passed = (max(errors.values(), default=0) <= SURFACE_TOLERANCE and max_bone_angle <= ANGLE_TOLERANCE
              and max_bone_position <= POSITION_TOLERANCE and max_root_foot <= POSITION_TOLERANCE)
    result = {"passed": passed, "samples": data["frames"] * 2 - 1, "integer_samples": data["frames"],
              "half_samples": data["frames"] - 1, "max_vertex_error_metres": errors,
              "integer_max_vertex_error_metres": errors_integer, "half_max_vertex_error_metres": errors_half,
              "worst_samples": worst, "max_bone_angle_error_degrees": max_bone_angle,
              "max_bone_position_error_metres": max_bone_position,
              "max_source_game_root_foot_error_metres": max_root_foot,
              "source_game_root_foot_errors_metres": root_foot_errors,
              "tolerances": {"surface_metres": SURFACE_TOLERANCE, "bone_position_metres": POSITION_TOLERANCE,
                             "root_foot_metres": POSITION_TOLERANCE, "bone_angle_degrees": ANGLE_TOLERANCE},
              "root_foot_comparison": "source versus candidate at each sample; source mocap movement is allowed",
              "metres_per_blender_unit": metres_per_unit}
    return result, sample_errors


def build_basis(train_frames):
    remove_owned_shapes()
    vectors = np.stack([residual_vector(frame) for frame in train_frames])
    # Accumulate a small sample Gram matrix in double precision in blocks.
    # Never construct the vastly larger vertex covariance matrix.
    gram = np.zeros((len(train_frames), len(train_frames)), dtype=np.float64)
    for start in range(0, feature_count, 65536):
        block = vectors[:, start:start + 65536].astype(np.float64)
        gram += block @ block.T
    eigenvalues, eigenvectors = np.linalg.eigh(gram)
    order = np.argsort(eigenvalues)[::-1]
    eigenvalues, eigenvectors = eigenvalues[order], eigenvectors[:, order]
    rank = min(MAX_RANK, int(np.count_nonzero(eigenvalues > max(1e-14, float(eigenvalues[0]) * 1e-12))))
    assert rank > 0, "No nonzero corrective basis; validate the baseline first"
    modes = ((eigenvectors[:, :rank].T @ vectors) / np.sqrt(eigenvalues[:rank])[:, None]).astype(np.float32)
    del vectors, gram, eigenvectors
    coefficients = np.empty((data["frames"], rank), dtype=np.float64)
    # Stream one exact residual at a time; do not keep 301 full mesh frames.
    for frame in range(1, data["frames"] + 1):
        coefficients[frame - 1] = modes @ residual_vector(frame)
    scale = np.maximum(np.max(np.abs(coefficients), axis=0), 1e-12)
    modes *= scale[:, None]
    coefficients /= scale[None, :]
    assert np.max(np.abs(coefficients)) <= 1.000001
    names = [PREFIX + "Corrective_" + str(index + 1).zfill(2) for index in range(rank)]
    for _, candidate in pairs:
        basis = candidate.data.shape_keys.reference_key
        base = np.empty(len(basis.data) * 3, dtype=np.float32)
        basis.data.foreach_get("co", base)
        for index, name in enumerate(names):
            key = candidate.shape_key_add(name=name, from_mix=False)
            key.relative_key = basis
            key.data.foreach_set("co", base + modes[index, offsets[candidate.name]])
            key.slider_min, key.slider_max, key.value = -1.0, 1.0, 0.0
        for frame in range(1, data["frames"] + 1):
            for index, name in enumerate(names):
                key = candidate.data.shape_keys.key_blocks[name]
                key.value = float(coefficients[frame - 1, index])
                key.keyframe_insert("value", frame=frame, group="KM466 Correctives")
        owned_paths = {candidate.data.shape_keys.key_blocks[name].path_from_id("value") for name in names}
        for curve in curves(shape_actions[candidate.name]):
            if curve.data_path in owned_paths:
                for point in curve.keyframe_points:
                    point.interpolation = "LINEAR"
    return rank, names


def nonzero_morph_names():
    names = set()
    for _, candidate in pairs:
        for key in candidate.data.shape_keys.key_blocks:
            if key == candidate.data.shape_keys.reference_key:
                continue
            values = np.empty(len(key.data) * 3, dtype=np.float32)
            relative = np.empty_like(values)
            key.data.foreach_get("co", values)
            key.relative_key.data.foreach_get("co", relative)
            if float(np.max(np.linalg.norm((values - relative).reshape((-1, 3)), axis=1), initial=0)) > 1e-7:
                names.add(key.name)
    return sorted(names)


validation = None
error_message = None
trials = []
try:
    set_clip()
    if PHASE == "validate":
        validation, _ = validate()
    else:
        remove_owned_shapes()
        # Avoid generating a basis when the isolated uncorrected clip passes.
        # Initial author validation can gate this without another 601 samples.
        initial_errors = data.get("max_vertex_error_metres", data.get("full_source_max_vertex_error_metres", {}))
        if data.get("passed") and initial_errors and max(initial_errors.values()) <= SURFACE_TOLERANCE:
            validation, _ = validate()
            rank, names, train_frames = 0, [], []
        else:
            train_frames = sorted({float(round(frame)) for frame in np.linspace(1, data["frames"], 49)})
            assert len(train_frames) == 49
            for training_pass in range(2):
                rank, names = build_basis(train_frames)
                validation, sample_errors = validate()
                trials.append({"rank": rank, "training_frames": train_frames,
                               "maximum_error_metres": max(validation["max_vertex_error_metres"].values()),
                               "passed": validation["passed"]})
                data.update(validation, corrective_rank=rank, training_frames=train_frames,
                            corrective_shapes=names, corrective_rank_trials=trials)
                REPORT.write_text(json.dumps(data, indent=2), encoding="utf-8")
                if validation["passed"]:
                    break
                if validation["max_bone_angle_error_degrees"] > ANGLE_TOLERANCE or validation["max_bone_position_error_metres"] > POSITION_TOLERANCE:
                    break  # Surface shapes cannot repair a failed retarget.
                if training_pass == 0:
                    # Add twelve measured error peaks, including half frames,
                    # while retaining the original broad 49-sample coverage.
                    additional = []
                    for _, frame in sorted(sample_errors, reverse=True):
                        if frame not in train_frames and frame not in additional:
                            additional.append(frame)
                        if len(additional) == 12:
                            break
                    train_frames = sorted(train_frames + additional)
                    assert len(train_frames) == 61
        data.update(corrective_rank=rank, training_frames=train_frames, corrective_shapes=names,
                    corrective_shape_names=names,
                    corrective_rank_trials=trials,
                    corrective_method="Uncentered Gram PCA of additive inverse normalized weighted LBS rest-space residuals",
                    coefficient_samples=data["frames"], coefficient_interpolation="LINEAR", maximum_corrective_rank=MAX_RANK,
                    corrective_signed_range=[-1.0, 1.0])
    data.update(validation)
    data["nonzero_morph_names"] = nonzero_morph_names()
    assert [bone.name for bone in game.data.bones] == bone_names
    for original, candidate in pairs:
        assert [key.name for key in candidate.data.shape_keys.key_blocks if not key.name.startswith(PREFIX)] == prior_names[candidate.name]
        assert ([key.name for key in original.data.shape_keys.key_blocks] if original.data.shape_keys else []) == source_shape_names[original.name]
    data["preserved_existing_correctives_and_source_shapes"] = True
    data["validation_phase"] = PHASE
    data["working_blend_sha256_before"] = before_sha256
    data["elapsed_seconds"] = time.monotonic() - started
    data["saved_editable_mvp"] = False
except Exception as exc:
    error_message = repr(exc)
    data.update(passed=False, corrective_failure=error_message, saved_editable_mvp=False,
                elapsed_seconds=time.monotonic() - started)
finally:
    # Restore the caller's working view, actions, FK properties and values even
    # when validation or inverse skinning fails. Only our owned data persists.
    for obj in (source, game):
        saved = rig_state[obj.name]
        restore_animation(obj, saved["animation"])
        obj.data.pose_position = saved["pose_position"]
    for name, owner in shape_owners.items():
        restore_animation(owner, shape_state[name]["animation"])
    for obj, name, show_viewport, show_render in modifier_state:
        modifier = obj.modifiers.get(name)
        if modifier:
            modifier.show_viewport, modifier.show_render = show_viewport, show_render
    for name, hidden in visibility_state.items():
        bpy.data.objects[name].hide_set(hidden)
    scene.frame_start, scene.frame_end = scene_state["start"], scene_state["end"]
    scene.render.fps, scene.render.fps_base = scene_state["fps"], scene_state["fps_base"]
    scene.frame_set(scene_state["frame"], subframe=scene_state["subframe"])
    for obj in (source, game):
        saved = rig_state[obj.name]
        for name, value in saved["transforms"].items():
            setattr(obj, name, value)
        obj.matrix_basis = saved["matrix_basis"]
        for name, value in saved["properties"].items():
            obj[name] = value
        for bone_name, (bone_transforms, properties, matrix_basis) in saved["bones"].items():
            bone = obj.pose.bones[bone_name]
            for name, value in bone_transforms.items():
                setattr(bone, name, value)
            for name, value in properties.items():
                bone[name] = value
            bone.matrix_basis = matrix_basis
    for name, owner in shape_owners.items():
        for key in owner.key_blocks:
            key.value = shape_state[name]["values"].get(key.name, 0.0)
    bpy.context.view_layer.update()

bpy.context.window.scene = previous_scene

if PHASE == "correct" and data.get("passed") and error_message is None:
    assert "FINISHED" in bpy.ops.wm.save_as_mainfile(filepath=str(WORKING), check_existing=False)
    data["saved_editable_mvp"] = True
    data["working_blend_sha256_after"] = hashlib.sha256(WORKING.read_bytes()).hexdigest()
if data.get("passed"):
    data.pop("corrective_failure", None)
REPORT.write_text(json.dumps(data, indent=2), encoding="utf-8")
result = {"report": str(REPORT), "phase": PHASE, "passed": data.get("passed", False),
          "corrective_rank": data.get("corrective_rank"), "training_samples": len(data.get("training_frames", [])),
          "max_vertex_error_metres": data.get("max_vertex_error_metres"),
          "max_bone_angle_error_degrees": data.get("max_bone_angle_error_degrees"),
          "max_source_game_root_foot_error_metres": data.get("max_source_game_root_foot_error_metres"),
          "saved_editable_mvp": data.get("saved_editable_mvp", False), "failure": error_message}
