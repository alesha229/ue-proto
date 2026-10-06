"""Soft regions of the game rig like Blender's shared physics cages (Blender MCP, Gratia_mvp.blend).

In the source, TitsPhys / AssPhys / ThighsPhys cloth cages deform skin and tight clothing together
(Surface Deform on Body, Top, Pants, Pants decor, Boots), and render masks hide the skin under
pants/boots/gloves. The game rig replaced the cages by soft bones (DEF-breast, DEF-ass) whose
weights differ between skin and clothing, and keeps the skin: skin and tights/top jiggled
separately and the skin poked through. This script, run through Blender MCP with a header
`PHASE = '...'`:

- apply: adds DEF-thigh_soft.L/R (ThighsPhys region, child of DEF-thigh) and, for every
  game-rig mesh vertex near the skin, sets the soft-bone weights to the skin's weights under it
  (barycentric transfer), moving the difference to/from the soft bone's parent. Soft bones are
  rigid with their parents in all clips, so clip deformation is unchanged (checked on a test
  pose); only physics/squash moves them. Skin covered by tight clothing is moved >= 2.5 mm
  under it (all shape keys alike) instead of being deleted.
- validate: rest-pose skin-through-clothing count and the test-pose deformation difference.
- export: mesh + armature FBX without animation (Gratia_Game_mesh.fbx) for the Unreal reimport.
- save: saves the working copy (backup first).
"""
import hashlib
import json
from pathlib import Path

import bpy
import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

ROOT = Path("E:/coding/ue proto")
EVIDENCE = ROOT / "evidence/05/soft_regions"
EVIDENCE.mkdir(parents=True, exist_ok=True)
PHASE = globals().get("PHASE", "validate")
assert PHASE == "restore" or Path(bpy.data.filepath).resolve() == (ROOT / "Exports/Gratia/Gratia_mvp.blend").resolve()
rig = bpy.data.objects["Gratia_GameRig"]
body = bpy.data.objects["Body_GameRig"]
meshes = [o for o in bpy.data.objects if o.type == "MESH" and o.find_armature() == rig]
TIGHT = ["Pants_GameRig", "Top_GameRig", "Boots_GameRig", "Gloves_GameRig"]
# soft bone -> parent it is rigid with in every clip
FAMILIES = {"DEF-breast.L": "DEF-spine.003", "DEF-breast.R": "DEF-spine.003", "DEF-ass.L": "DEF-spine", "DEF-ass.R": "DEF-spine",
            "DEF-thigh_soft.L": "DEF-thigh.L", "DEF-thigh_soft.R": "DEF-thigh.R",
            "DEF-thigh_soft.L.001": "DEF-thigh.L.001", "DEF-thigh_soft.R.001": "DEF-thigh.R.001"}
THIGH_SOFT_SHARE = 0.5      # of the skin's ThighsPhys weight, taken from DEF-thigh
NEAR_FULL, NEAR_NONE = 0.010, 0.030   # m: full skin weights up to 1 cm from the skin, own weights beyond 3 cm
CLEARANCE, COVER_RAY, MAX_PUSH = 0.0025, 0.025, 0.012
TEST_POSE = {"DEF-spine": (10, 0, 0), "DEF-spine.003": (0, 0, 15), "DEF-thigh.L": (-35, 0, 10), "DEF-thigh.R": (20, 0, -5),
             "DEF-thigh.L.001": (0, 20, 0), "DEF-shoulder.L": (0, 0, 12), "DEF-upper_arm.R": (40, 0, 0)}
# Clothing that moved with a cage over the skin takes the skin's whole weight set there (second
# skin): mesh -> its cage group. Elsewhere only soft-bone weights are matched (clips unchanged).
SECOND_SKIN = {"Top_GameRig": "TitsPhys"}


def group_weights(obj, name):
    g = obj.vertex_groups.get(name)
    out = np.zeros(len(obj.data.vertices))
    if g is None:
        return out
    for v in obj.data.vertices:
        for e in v.groups:
            if e.group == g.index:
                out[v.index] = e.weight
    return out


def set_weights(obj, name, values):
    g = obj.vertex_groups.get(name) or obj.vertex_groups.new(name=name)
    zero = [i for i, w in enumerate(values) if w <= 1e-5]
    if zero:
        g.remove(zero)
    for i, w in enumerate(values):
        if w > 1e-5:
            g.add([i], float(w), "REPLACE")


def rest_points(obj):
    return [obj.matrix_world @ v.co for v in obj.data.vertices]


def add_thigh_bones():
    if "DEF-thigh_soft.L" in rig.data.bones:
        return False
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    override = dict(active_object=rig, object=rig, selected_objects=[rig], selected_editable_objects=[rig])
    with bpy.context.temp_override(**override):
        bpy.ops.object.mode_set(mode="EDIT")
    for side in ("L", "R"):
        for suffix in ("", ".001"):
            thigh = rig.data.edit_bones["DEF-thigh.%s%s" % (side, suffix)]
            axis = (thigh.tail - thigh.head).normalized()
            length = (thigh.tail - thigh.head).length
            bone = rig.data.edit_bones.new("DEF-thigh_soft.%s%s" % (side, suffix))
            bone.head = thigh.head + axis * 0.1 * length
            bone.tail = thigh.head + axis * 0.9 * length
            bone.roll = thigh.roll
            bone.parent = thigh
            bone.use_connect = False
            bone.use_deform = True
    with bpy.context.temp_override(**override):
        bpy.ops.object.mode_set(mode="OBJECT")
    return True


def deformed(objs):
    dg = bpy.context.evaluated_depsgraph_get()
    out = {}
    for o in objs:
        ev = o.evaluated_get(dg)
        m = ev.to_mesh()
        out[o.name] = np.array([ev.matrix_world @ v.co for v in m.vertices])
        ev.to_mesh_clear()
    return out


SAVED_POSE = {}


def test_pose(on):
    """A posed rig (spine, chest, both thighs) to compare deformation before/after weight edits."""
    saved_action = rig.animation_data.action if rig.animation_data else None
    if on:
        SAVED_POSE["action"] = saved_action
        if rig.animation_data:
            rig.animation_data.action = None
    for name, (x, y, z) in TEST_POSE.items():
        pb = rig.pose.bones[name]
        if on:
            SAVED_POSE[name] = (pb.rotation_mode, pb.rotation_quaternion.copy(), pb.rotation_euler.copy())
            pb.rotation_mode = "QUATERNION"
            pb.rotation_quaternion = Matrix.Rotation(np.radians(x), 3, "X").to_quaternion() @                 Matrix.Rotation(np.radians(y), 3, "Y").to_quaternion() @ Matrix.Rotation(np.radians(z), 3, "Z").to_quaternion()
        elif name in SAVED_POSE:
            mode, quat, euler = SAVED_POSE[name]
            pb.rotation_mode = mode; pb.rotation_quaternion = quat; pb.rotation_euler = euler
    if not on and rig.animation_data:
        rig.animation_data.action = SAVED_POSE.get("action")
    bpy.context.view_layer.update()


def skin_poke_count():
    saved = rig.data.pose_position
    rig.data.pose_position = "REST"
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    ev = body.evaluated_get(dg)
    bm = ev.to_mesh()
    pts = [ev.matrix_world @ v.co for v in bm.vertices]
    nrm = [(ev.matrix_world.to_3x3() @ v.normal).normalized() for v in bm.vertices]
    result = {}
    for name in TIGHT:
        cloth = bpy.data.objects[name].evaluated_get(dg)
        cm = cloth.to_mesh()
        tree = BVHTree.FromPolygons([cloth.matrix_world @ v.co for v in cm.vertices], [p.vertices[:] for p in cm.polygons])
        covered = outside = 0
        for p, n in zip(pts, nrm):
            hit = tree.ray_cast(p - n * 0.001, n, COVER_RAY)
            if hit[0] is None:
                continue
            covered += 1
            near = tree.find_nearest(p, COVER_RAY)
            if near[0] is not None and (p - near[0]).dot(near[1]) > -0.0005:
                outside += 1
        result[name] = {"covered": covered, "within_0_5mm_or_outside": outside}
        cloth.to_mesh_clear()
    ev.to_mesh_clear()
    rig.data.pose_position = saved
    return result


def save_working_copy():
    """Save over Gratia_mvp.blend; retried while another process (git-lfs status) holds the file."""
    import time
    bpy.context.preferences.filepaths.save_version = 0
    target = ROOT / "Exports/Gratia/Gratia_mvp.blend"
    for attempt in range(12):
        try:
            assert "FINISHED" in bpy.ops.wm.save_as_mainfile(filepath=str(target), check_existing=False)
            return attempt
        except RuntimeError:
            stale = Path(str(target) + "@")
            if stale.exists():
                stale.unlink()
            time.sleep(5)
    raise RuntimeError("Gratia_mvp.blend stayed locked")


result = {"phase": PHASE}
if PHASE == "apply":
    backup = EVIDENCE / "Gratia_mvp_before_soft_regions.blend"
    if not backup.exists():
        import shutil
        shutil.copyfile(ROOT / "Exports/Gratia/Gratia_mvp.blend", backup)
    rest = deformed([o for o in meshes if o is not body])
    test_pose(True)
    before = deformed([o for o in meshes if o is not body])
    test_pose(False)
    result["test_pose_motion_mm"] = round(float(max(np.abs(before[k] - rest[k]).max() for k in rest) * 1000), 1)
    result["thigh_bones_added"] = add_thigh_bones()

    # Skin truth: existing soft weights; thighs get a share of ThighsPhys from DEF-thigh.
    skin = {bone: group_weights(body, bone) for bone in FAMILIES}
    thighs_region = group_weights(body, "ThighsPhys")
    xs = np.array([p.x for p in rest_points(body)])
    for side, sign in (("L", 1), ("R", -1)):
        # From the upper thigh bone first, then its lower segment (DEF-thigh.001 twists slightly
        # against it in clips; measured on the test pose with a knee twist).
        upper = group_weights(body, "DEF-thigh." + side)
        lower = group_weights(body, "DEF-thigh.%s.001" % side)
        want = np.where(xs * sign > 0, THIGH_SOFT_SHARE * thighs_region, 0.0)
        from_upper = np.minimum(want, upper)
        from_lower = np.minimum(want - from_upper, lower)
        skin["DEF-thigh_soft." + side] = from_upper
        skin["DEF-thigh_soft.%s.001" % side] = from_lower
        set_weights(body, "DEF-thigh_soft." + side, from_upper)
        set_weights(body, "DEF-thigh_soft.%s.001" % side, from_lower)
        set_weights(body, "DEF-thigh." + side, upper - from_upper)
        set_weights(body, "DEF-thigh.%s.001" % side, lower - from_lower)
        result.setdefault("thigh_soft_verts", {})[side] = {"upper_over_0_2": int((from_upper > 0.2).sum()),
                                                          "lower_over_0_2": int((from_lower > 0.2).sum())}

    # Skin weights of every vertex (all deform groups) for the second-skin transfer.
    body_names = {g.index: g.name for g in body.vertex_groups}
    skin_all = [{body_names[e.group]: e.weight for e in v.groups if body_names[e.group].startswith("DEF-")} for v in body.data.vertices]
    for side in ("L", "R"):
        for suffix in ("", ".001"):
            soft_name, parent_name = "DEF-thigh_soft.%s%s" % (side, suffix), "DEF-thigh.%s%s" % (side, suffix)
            for i, w in enumerate(skin[soft_name]):
                if w > 1e-5:
                    skin_all[i][soft_name] = float(w)
                    skin_all[i][parent_name] = max(0.0, skin_all[i].get(parent_name, 0.0) - float(w))
    result["second_skin"] = {}
    # Transfer to every other mesh near the skin (barycentric on the nearest skin face).
    tree = BVHTree.FromPolygons(rest_points(body), [p.vertices[:] for p in body.data.polygons])
    polys = [p.vertices[:] for p in body.data.polygons]
    body_pts = np.array([tuple(p) for p in rest_points(body)])
    stats = {}
    for obj in meshes:
        if obj is body:
            continue
        pts = rest_points(obj)
        own = {bone: group_weights(obj, bone) for bone in FAMILIES}
        parents = {p: group_weights(obj, p) for p in set(FAMILIES.values())}
        changed = clamped = 0
        target = {bone: own[bone].copy() for bone in FAMILIES}
        cage = group_weights(obj, SECOND_SKIN[obj.name]) if obj.name in SECOND_SKIN else None
        second = {}
        for i, p in enumerate(pts):
            near = tree.find_nearest(p, NEAR_NONE)
            if near[0] is None:
                continue
            if cage is not None and cage[i] > 0.05:
                blend = float(np.clip((NEAR_NONE - near[3]) / (NEAR_NONE - NEAR_FULL), 0.0, 1.0)) * float(min(1.0, cage[i] * 2.0))
                face = polys[near[2]]
                d = np.linalg.norm(body_pts[list(face)] - np.array(tuple(near[0])), axis=1)
                bary = 1.0 / np.maximum(d, 1e-6); bary /= bary.sum()
                mixed = {}
                for k, vi in enumerate(face):
                    for name, w in skin_all[vi].items():
                        mixed[name] = mixed.get(name, 0.0) + blend * bary[k] * w
                second[i] = (blend, mixed)
                continue
            blend = float(np.clip((NEAR_NONE - near[3]) / (NEAR_NONE - NEAR_FULL), 0.0, 1.0))
            face = polys[near[2]]
            d = np.linalg.norm(body_pts[list(face)] - np.array(tuple(near[0])), axis=1)
            bary = 1.0 / np.maximum(d, 1e-6)
            bary /= bary.sum()
            for bone in FAMILIES:
                target[bone][i] = blend * float(skin[bone][list(face)] @ bary) + (1.0 - blend) * own[bone][i]
        for bone, parent in FAMILIES.items():
            delta = target[bone] - own[bone]
            limit = parents[parent]
            over = delta > limit
            clamped += int(over.sum())
            delta = np.minimum(delta, limit)
            if np.abs(delta).max(initial=0) < 1e-5:
                continue
            changed += int((np.abs(delta) > 1e-4).sum())
            parents[parent] = parents[parent] - delta
            set_weights(obj, bone, own[bone] + delta)
        for parent, values in parents.items():
            if obj.vertex_groups.get(parent) is not None or values.max(initial=0) > 1e-5:
                set_weights(obj, parent, np.maximum(values, 0.0))
        if second:
            names = {g.index: g.name for g in obj.vertex_groups}
            for i, (blend, mixed) in second.items():
                v = obj.data.vertices[i]
                own_all = {names[e.group]: e.weight for e in v.groups if names[e.group].startswith("DEF-")}
                total = sum(own_all.values()) or 1.0
                final = {n: (1.0 - blend) * w / total for n, w in own_all.items()}
                for n, w in mixed.items():
                    final[n] = final.get(n, 0.0) + w
                norm = sum(final.values()) or 1.0
                for n in own_all:
                    obj.vertex_groups[n].remove([i])
                for n, w in final.items():
                    if w / norm > 1e-4:
                        (obj.vertex_groups.get(n) or obj.vertex_groups.new(name=n)).add([i], w / norm, "REPLACE")
            result["second_skin"][obj.name] = {"vertices": len(second), "full_blend": int(sum(1 for b, _ in second.values() if b > 0.99))}
        stats[obj.name] = {"changed": changed, "clamped": clamped}
    result["weights"] = stats

    # Covered skin under tight clothing: at least CLEARANCE inside, all shape keys alike.
    dg = bpy.context.evaluated_depsgraph_get()
    offsets = np.zeros((len(body.data.vertices), 3))
    normals = [v.normal.copy() for v in body.data.vertices]
    for name in TIGHT:
        cloth = bpy.data.objects[name]
        ctree = BVHTree.FromPolygons(rest_points(cloth), [p.vertices[:] for p in cloth.data.polygons])
        for i, (p, n) in enumerate(zip(rest_points(body), normals)):
            if ctree.ray_cast(p - n * 0.001, n, COVER_RAY)[0] is None:
                continue
            near = ctree.find_nearest(p, COVER_RAY)
            if near[0] is None:
                continue
            cloth_normal = near[1] if near[1].dot(n) >= 0 else -near[1]
            signed = (p - near[0]).dot(cloth_normal)
            if signed > -CLEARANCE:
                push = min(MAX_PUSH, signed + CLEARANCE)
                cand = -np.array(tuple(cloth_normal)) * push
                if np.linalg.norm(cand) > np.linalg.norm(offsets[i]):
                    offsets[i] = cand
    moved = np.linalg.norm(offsets, axis=1) > 0
    neighbours = [[] for _ in body.data.vertices]
    for e in body.data.edges:
        a, b = e.vertices
        neighbours[a].append(b); neighbours[b].append(a)
    for _ in range(2):
        smooth = offsets.copy()
        for i in range(len(offsets)):
            if neighbours[i] and (moved[i] or any(moved[j] for j in neighbours[i])):
                avg = offsets[neighbours[i]].mean(axis=0)
                smooth[i] = offsets[i] if np.linalg.norm(offsets[i]) > np.linalg.norm(avg) else 0.5 * (offsets[i] + avg)
        offsets = smooth
    flat = offsets.reshape(-1).astype(np.float64)
    base = np.empty(len(flat))
    body.data.vertices.foreach_get("co", base)
    body.data.vertices.foreach_set("co", base + flat)
    for key in body.data.shape_keys.key_blocks:
        co = np.empty(len(flat))
        key.data.foreach_get("co", co)
        key.data.foreach_set("co", co + flat)
    body.data.update()
    result["skin_pushed"] = {"vertices": int((np.linalg.norm(offsets, axis=1) > 1e-5).sum()),
                             "max_mm": round(float(np.linalg.norm(offsets, axis=1).max() * 1000), 2)}

    test_pose(True)
    after = deformed([o for o in meshes if o is not body])
    test_pose(False)
    result["test_pose_max_mm"] = {k: round(float(np.abs(after[k] - before[k]).max() * 1000), 4) for k in before}
    result["test_pose_p99_mm"] = {k: round(float(np.percentile(np.linalg.norm(after[k] - before[k], axis=1), 99) * 1000), 3) for k in SECOND_SKIN}
elif PHASE == "restore":
    # Back to the state before any soft-region edit (backup of the first apply), saved as the working copy.
    bpy.ops.wm.open_mainfile(filepath=str(EVIDENCE / "Gratia_mvp_before_soft_regions.blend"))
    result["restored"] = True
    result["save_retries"] = save_working_copy()
elif PHASE == "revert":
    # Discard unsaved edits of this session: reload the working copy from disk.
    path = str(ROOT / "Exports/Gratia/Gratia_mvp.blend")
    bpy.ops.wm.open_mainfile(filepath=path)
    result["reloaded"] = path
elif PHASE == "validate":
    result["rest_skin_through_clothing"] = skin_poke_count()
elif PHASE == "export":
    selected = [rig] + meshes
    for obj in bpy.context.scene.objects:
        obj.select_set(False)
    for obj in selected:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = rig
    saved_action = rig.animation_data.action if rig.animation_data else None
    if rig.animation_data:
        rig.animation_data.action = None
    for pb in rig.pose.bones:
        pb.location = (0, 0, 0); pb.rotation_quaternion = (1, 0, 0, 0); pb.rotation_euler = (0, 0, 0); pb.scale = (1, 1, 1)
    path = ROOT / "Exports/Gratia/GameRig/Gratia_Game_mesh.fbx"
    with bpy.context.temp_override(active_object=rig, object=rig, selected_objects=selected, selected_editable_objects=selected):
        assert "FINISHED" in bpy.ops.export_scene.fbx(
            filepath=str(path), use_selection=True, object_types={"MESH", "ARMATURE"}, global_scale=1,
            apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS", axis_forward="-Y", axis_up="Z",
            use_mesh_modifiers=False, mesh_smooth_type="FACE", add_leaf_bones=False,
            use_armature_deform_only=True, armature_nodetype="NULL", path_mode="AUTO", embed_textures=False, bake_anim=False)
    if rig.animation_data:
        rig.animation_data.action = saved_action
    with path.open("rb") as stream:
        result.update(fbx=str(path), bytes=path.stat().st_size, sha256=hashlib.file_digest(stream, "sha256").hexdigest())
elif PHASE == "save":
    result["save_retries"] = save_working_copy()
    with (ROOT / "Exports/Gratia/Gratia_mvp.blend").open("rb") as stream:
        result["saved_sha256"] = hashlib.file_digest(stream, "sha256").hexdigest()
(EVIDENCE / ("soft_regions_%s.json" % PHASE)).write_text(json.dumps(result, indent=2), encoding="utf-8")
print(json.dumps(result))
