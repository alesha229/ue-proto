"""Compact inverse-LBS corrective bases for reference reactions, via Blender MCP.

Training uses a small sample-by-sample Gram matrix, never a vertex covariance
matrix. Existing source shapes, nine Game_ correctives and previous clips are
retained. Export is gated by full integer/half-frame surface validation.
"""
import bpy
import hashlib
import json
import math
from pathlib import Path

import numpy as np

ROOT = Path("E:/coding/ue proto")
OUT = ROOT / "Exports/Gratia/GameRig"
EVIDENCE = ROOT / "evidence/04/blender_mcp"
REPORT = EVIDENCE / "reference_reactions_validation.json"
assert Path(bpy.data.filepath).resolve() == (ROOT / "Exports/Gratia/Gratia_mvp.blend").resolve()
source, game = bpy.data.objects["Gratia"], bpy.data.objects["Gratia_GameRig"]
scene = bpy.context.scene
manifest = json.loads((OUT / "game_rig_manifest.json").read_text(encoding="utf-8"))
data = json.loads(REPORT.read_text(encoding="utf-8"))
pairs = [(bpy.data.objects[m["source"]], bpy.data.objects[m["candidate"]]) for m in manifest["meshes"]]


def curves(action):
    return [c for layer in action.layers for strip in layer.strips for bag in strip.channelbags for c in bag.fcurves]


def remove_curve(action, curve):
    for layer in action.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                if curve in list(bag.fcurves):
                    bag.fcurves.remove(curve)
                    return


# A failed authoring pass can leave these newly owned keys in memory. Reset only
# this script's names and channels, never the existing nine corrective shapes.
for _, candidate in pairs:
    for key in list(candidate.data.shape_keys.key_blocks):
        if key.name.startswith(("Game_HandRef_", "Game_CheerRef_")):
            path = key.path_from_id("value")
            for action in bpy.data.actions:
                for curve in curves(action):
                    if curve.data_path == path:
                        remove_curve(action, curve)
            candidate.shape_key_remove(key)
for clip in data["clips"]:
    clip.pop("reference_corrective_shapes", None)
    clip.pop("corrective_rank_trials", None)
data["old_clip_regression"] = []
bone_names = [bone.name for bone in game.data.bones]
original_shape_names = {obj.name: [key.name for key in obj.data.shape_keys.key_blocks if not key.name.startswith("Game_")]
                        for _, obj in pairs}
old_game_shapes = {obj.name: [key.name for key in obj.data.shape_keys.key_blocks if key.name.startswith("Game_")]
                   for _, obj in pairs}
assert all(len(names) == 9 for names in old_game_shapes.values())


def frame_set(frame):
    scene.frame_set(math.floor(frame), subframe=frame % 1)
    bpy.context.view_layer.update()
    return bpy.context.evaluated_depsgraph_get()


def coordinates(obj, depsgraph, world=False):
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    values = np.empty(len(mesh.vertices) * 3, dtype=np.float32)
    mesh.vertices.foreach_get("co", values)
    result = values.reshape((-1, 3))
    if world:
        matrix = np.array(evaluated.matrix_world, dtype=np.float32)
        result = result @ matrix[:3, :3].T + matrix[:3, 3]
    evaluated.to_mesh_clear()
    return result


weights = {}
offsets = {}
vertex_total = 0
for original, candidate in pairs:
    indices, names, amounts = [], [], []
    for vertex in candidate.data.vertices:
        for group in vertex.groups:
            name = candidate.vertex_groups[group.group].name
            if name in game.data.bones and group.weight > 1e-8:
                indices.append(vertex.index); names.append(name); amounts.append(group.weight)
    weights[candidate.name] = (np.array(indices), names, np.array(amounts, dtype=np.float32))
    offsets[candidate.name] = slice(vertex_total * 3, (vertex_total + len(candidate.data.vertices)) * 3)
    vertex_total += len(candidate.data.vertices)


def set_clip(clip):
    source.animation_data.action = bpy.data.actions[clip["source_action"]]
    game.animation_data.action = bpy.data.actions[clip["game_action"]]
    for original, candidate in pairs:
        candidate.data.shape_keys.animation_data.action = bpy.data.actions[clip["corrective_actions"][candidate.name]]
        for key in candidate.data.shape_keys.key_blocks:
            if key.name.startswith(("Game_HandRef_", "Game_CheerRef_")):
                key.value = 0
        if original.data.shape_keys:
            prefix = "Reference_" + clip["clip"] + "_SourceShapes_" + original.name
            action = bpy.data.actions.get(prefix)
            assert action, prefix
            original.data.shape_keys.animation_data.action = action


def inverse_delta(candidate, difference, depsgraph):
    rig = game.evaluated_get(depsgraph)
    indices, names, amounts = weights[candidate.name]
    skin = {name: np.array(rig.pose.bones[name].matrix @ game.data.bones[name].matrix_local.inverted(), dtype=np.float32)[:3, :3]
            for name in set(names)}
    matrices = np.zeros((len(candidate.data.vertices), 3, 3), dtype=np.float32)
    total = np.zeros(len(candidate.data.vertices), dtype=np.float32)
    np.add.at(matrices, indices, np.array([skin[name] for name in names], dtype=np.float32) * amounts[:, None, None])
    np.add.at(total, indices, amounts)
    mask = total > 1e-8
    matrices[mask] /= total[mask, None, None]
    matrices[~mask] = np.eye(3, dtype=np.float32)
    return np.linalg.solve(matrices, difference[..., None])[..., 0]


def residual_vector(frame):
    depsgraph = frame_set(frame)
    result = np.empty(vertex_total * 3, dtype=np.float32)
    for original, candidate in pairs:
        difference = coordinates(original, depsgraph) - coordinates(candidate, depsgraph)
        result[offsets[candidate.name]] = inverse_delta(candidate, difference, depsgraph).ravel()
    assert np.isfinite(result).all()
    return result


def validate(clip):
    errors = {original.name: 0.0 for original, _ in pairs}
    worst = {}
    feet = None
    drift = 0.0
    for frame in np.arange(1, clip["frames"] + 0.1, 0.5):
        depsgraph = frame_set(float(frame))
        rig = game.evaluated_get(depsgraph)
        current = {name: rig.pose.bones[name].matrix.translation.copy() for name in ("root", "DEF-foot.L", "DEF-foot.R")}
        if feet is None: feet = current
        drift = max(drift, max((current[name] - feet[name]).length for name in feet))
        for original, candidate in pairs:
            distance = np.linalg.norm(coordinates(original, depsgraph, True) - coordinates(candidate, depsgraph, True), axis=1)
            assert np.isfinite(distance).all()
            value = float(distance.max(initial=0))
            if value > errors[original.name]:
                errors[original.name] = value
                worst[original.name] = {"frame": float(frame), "vertex": int(distance.argmax()), "error_metres": value}
    (EVIDENCE / "latest_reference_validation_worst.json").write_text(json.dumps(worst, indent=2), encoding="utf-8")
    return errors, drift


basis_records = []
for clip in data["clips"]:
    if clip["clip"] not in ("ReactHand", "ReactCheer"):
        continue
    set_clip(clip)
    # 37 samples * 72376 vertices * xyz floats is approximately 32 MB.
    train_frames = list(range(1, clip["frames"] + 1, 2))
    vectors = np.stack([residual_vector(frame) for frame in train_frames])
    gram = vectors @ vectors.T
    values, eigenvectors = np.linalg.eigh(gram.astype(np.float64))
    order = np.argsort(values)[::-1]
    values, eigenvectors = values[order], eigenvectors[:, order]
    rank_limit = min(8, int(np.sum(values > max(1e-12, values[0] * 1e-10))))
    modes = (eigenvectors[:, :rank_limit].T.astype(np.float32) @ vectors)
    modes /= np.sqrt(values[:rank_limit]).astype(np.float32)[:, None]
    # Project exact rest-space errors at all integer frames before adding basis
    # shapes; otherwise residual measurement would depend on previous PCA curves.
    coeff = np.stack([modes @ residual_vector(frame) for frame in range(1, clip["frames"] + 1)])
    coeff[0] = coeff[-1] = 0.0
    scales = np.max(np.abs(coeff), axis=0)
    modes *= scales[:, None]
    coeff /= np.maximum(scales, 1e-12)[None, :]
    prefix = "Game_HandRef_" if clip["clip"] == "ReactHand" else "Game_CheerRef_"
    mode_names = []
    for mode in range(rank_limit):
        name = prefix + str(mode + 1)
        mode_names.append(name)
        for original, candidate in pairs:
            basis = candidate.data.shape_keys.key_blocks["Basis"]
            base = np.empty(len(basis.data) * 3, dtype=np.float32)
            basis.data.foreach_get("co", base)
            shape = candidate.shape_key_add(name=name, from_mix=False)
            shape.data.foreach_set("co", base + modes[mode, offsets[candidate.name]])
            shape.slider_min, shape.slider_max = -1.0, 1.0
            shape.value = 0.0
    best = None
    trials = []
    for rank in (4, 6, 8):
        rank = min(rank, rank_limit)
        for original, candidate in pairs:
            for frame in range(1, clip["frames"] + 1):
                for index, name in enumerate(mode_names):
                    key = candidate.data.shape_keys.key_blocks[name]
                    key.value = float(coeff[frame - 1, index]) if index < rank else 0.0
                    key.keyframe_insert("value", frame=frame, group="Reference Correctives")
            for curve in curves(candidate.data.shape_keys.animation_data.action):
                for key in curve.keyframe_points: key.interpolation = "LINEAR"
        errors, drift = validate(clip)
        maximum = max(errors.values())
        trials.append({"rank": rank, "maximum_error_metres": maximum, "errors": errors, "root_foot_drift_metres": drift})
        data["corrective_in_progress"] = {"clip": clip["clip"], "trials": trials}
        REPORT.write_text(json.dumps(data, indent=2), encoding="utf-8")
        if maximum <= 0.001 and drift <= 0.001:
            best = rank
            break
        if rank == rank_limit:
            break
    assert best is not None, ("Corrective rank limit still fails", clip["clip"], trials)
    # Remove zero, unused PCA modes. They do not inflate the exported mesh.
    for original, candidate in pairs:
        action = candidate.data.shape_keys.animation_data.action
        for name in mode_names[best:]:
            paths = [c for c in curves(action) if c.data_path == 'key_blocks["' + name + '"].value']
            for curve in paths:
                remove_curve(action, curve)
            candidate.shape_key_remove(candidate.data.shape_keys.key_blocks[name])
    clip.update(max_vertex_error_metres=trials[-1]["errors"], root_foot_drift_metres=trials[-1]["root_foot_drift_metres"],
                passed=True, reference_corrective_shapes=mode_names[:best], corrective_rank_trials=trials)
    basis_records.append({"clip": clip["clip"], "new_shapes": mode_names[:best], "training_samples": len(train_frames),
                          "feature_count": vertex_total * 3, "matrix_bytes": vectors.nbytes, "trials": trials})
    REPORT.write_text(json.dumps(data, indent=2), encoding="utf-8")
    del vectors, gram, values, eigenvectors, modes, coeff

# A different clip's PCA curves must remain zero. Author explicit zero channels
# for all new shapes in each old and new candidate animation action.
new_shapes = sorted({name for item in basis_records for name in item["new_shapes"]})
for clip in data["clips"]:
    active = set(clip.get("reference_corrective_shapes", []))
    for original, candidate in pairs:
        candidate.data.shape_keys.animation_data.action = bpy.data.actions[clip["corrective_actions"][candidate.name]]
        for name in new_shapes:
            if name in active: continue
            key = candidate.data.shape_keys.key_blocks[name]
            key.value = 0.0
            key.keyframe_insert("value", frame=1); key.keyframe_insert("value", frame=clip["frames"])
for clip_name, rig_action, source_action in [
    ("Idle", json.loads(game["mvp_game_actions"])["Idle"], json.loads(game["mvp_source_actions"])["Idle"]),
    ("TestArms", json.loads(game["mvp_game_actions"])["TestArms"], json.loads(game["mvp_source_actions"])["TestArms"]),
    ("TestHead", json.loads(game["mvp_game_actions"])["TestHead"], json.loads(game["mvp_source_actions"])["TestHead"]),
    ("ReactSoft", "Gratia_Game_ReactSoft", "MVP_Source_ReactSoft"),
    ("ReactBright", "Gratia_Game_ReactBright", "MVP_Source_ReactBright"),
]:
    source.animation_data.action = bpy.data.actions[source_action]
    game.animation_data.action = bpy.data.actions[rig_action]
    for original, candidate in pairs:
        old_name = candidate.get("mvp_corrective_action_" + clip_name) if clip_name in ("Idle", "TestArms", "TestHead") else clip_name + "_Correctives_" + candidate.name
        old_action = bpy.data.actions[old_name]
        candidate.data.shape_keys.animation_data.action = old_action
        for candidate_key in candidate.data.shape_keys.key_blocks:
            if not candidate_key.name.startswith("Game_") and candidate_key.name != "Basis":
                candidate_key.value = 0.0
        for original_key in original.data.shape_keys.key_blocks if original.data.shape_keys else []:
            if original_key.name != "Basis": original_key.value = 0.0
        if original.data.shape_keys: original.data.shape_keys.animation_data.action = None
        for name in new_shapes:
            key = candidate.data.shape_keys.key_blocks[name]
            key.value = 0.0
            key.keyframe_insert("value", frame=1)
            key.keyframe_insert("value", frame=int(game.animation_data.action.frame_range[1]))
    test = {"frames": int(game.animation_data.action.frame_range[1])}
    errors, drift = validate(test)
    assert max(errors.values()) <= 0.001 and drift <= 0.001, ("Previous clip regressed", clip_name, errors, drift)
    data.setdefault("old_clip_regression", []).append({"clip": clip_name, "samples": test["frames"] * 2 - 1,
                                                     "errors": errors, "root_foot_drift_metres": drift, "passed": True})

# Confirm all final clips after every basis and explicit zero channel is present.
for clip in data["clips"]:
    set_clip(clip)
    errors, drift = validate(clip)
    assert max(errors.values()) <= 0.001 and drift <= 0.001, ("Final clip failed", clip["clip"], errors, drift)
    clip.update(max_vertex_error_metres=errors, root_foot_drift_metres=drift, passed=True)

assert [bone.name for bone in game.data.bones] == bone_names
for original, candidate in pairs:
    final = [key.name for key in candidate.data.shape_keys.key_blocks]
    assert all(name in final for name in original_shape_names[candidate.name] + old_game_shapes[candidate.name])
data["basis_records"] = basis_records
data["new_shape_names"] = new_shapes
data["candidate_shape_count_with_basis"] = len(pairs[0][1].data.shape_keys.key_blocks)
data["bone_name_sha256"] = hashlib.sha256("\n".join(bone_names).encode()).hexdigest()
data["original_shape_name_sha256"] = hashlib.sha256(json.dumps(original_shape_names, sort_keys=True).encode()).hexdigest()
data.pop("corrective_in_progress", None)
REPORT.write_text(json.dumps(data, indent=2), encoding="utf-8")
frame_set(1)
assert "FINISHED" in bpy.ops.wm.save_as_mainfile(filepath=str(ROOT / "Exports/Gratia/Gratia_mvp.blend"), check_existing=False)
result = {"report": str(REPORT), "new_shapes": new_shapes, "clips": [{k: c[k] for k in ("clip", "passed", "max_vertex_error_metres")} for c in data["clips"]],
          "old_clip_regression": data["old_clip_regression"], "bone_name_sha256": data["bone_name_sha256"],
          "candidate_shape_count_with_basis": data["candidate_shape_count_with_basis"]}
