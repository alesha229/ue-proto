"""Import MCP-validated reference cues while preserving the current game skeleton/settings.

Active post-MCP pipeline. Legacy import_gratia_game_rig.py/import_gratia_reactions.py
are initial migration scripts and must not be rerun over artist-edited assets.
"""
import hashlib
import json
import shutil
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "evidence/04"
REPORT_PATH = OUT / "blender_mcp/reference_reactions_validation.json"
report = json.loads(REPORT_PATH.read_text(encoding="utf-8"))
assert all(c["passed"] and c["exported"] for c in report["clips"])
for export in report["exports"]:
    file = Path(export["path"])
    assert file.is_file() and file.stat().st_size == export["bytes"], str(file)
    assert hashlib.sha256(file.read_bytes()).hexdigest() == export["sha256"], str(file)

lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
directory = "/Game/Gratia/GameRig"
mesh = lib.load_asset(directory + "/SK_Gratia_Game")
profile = lib.load_asset("/Game/Characters/Profiles/DA_Gratia")
assert isinstance(mesh, unreal.SkeletalMesh) and isinstance(profile, unreal.GratiaCharacterProfile)
skeleton = mesh.get_editor_property("skeleton")
physics = mesh.get_editor_property("physics_asset")
materials = list(mesh.get_editor_property("materials"))
old_morphs = {m.get_name() for m in mesh.get_editor_property("morph_targets")}


def bones(asset):
    component = unreal.SkeletalMeshComponent()
    component.set_skinned_asset_and_update(asset)
    return [str(component.get_bone_name(i)) for i in range(component.get_num_bones())]


original_bones = bones(mesh)
backup = OUT / "before_reference_import"
backup.mkdir(parents=True, exist_ok=True)
for asset in (mesh, skeleton, physics, profile):
    package = asset.get_path_name().split(".")[0].removeprefix("/Game/")
    source = ROOT / "GratiaVR/Content" / (package + ".uasset")
    assert source.is_file(), str(source)
    destination = backup / source.name
    if not destination.exists():
        shutil.copy2(source, destination)

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level("/Game/Gratia/Maps/L_Stage1")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, "Interchange.FeatureFlags.Import.FBX 0")


def import_asset(filename, name, options):
    task = unreal.AssetImportTask()
    for key, value in dict(filename=str(filename), destination_path=directory,
                           destination_name=name, automated=True, replace_existing=True,
                           replace_existing_settings=True, save=True,
                           factory=unreal.FbxFactory(), options=options).items():
        task.set_editor_property(key, value)
    tools.import_asset_tasks([task])
    assets = [lib.load_asset(path) for path in task.get_editor_property("imported_object_paths")]
    assert assets, "Import returned no objects: " + name
    return assets


options = unreal.FbxImportUI()
for key, value in dict(import_mesh=True, import_as_skeletal=True, import_animations=False,
                       import_materials=False, import_textures=False, create_physics_asset=False,
                       skeleton=skeleton, physics_asset=physics,
                       automated_import_should_detect_type=False,
                       mesh_type_to_import=unreal.FBXImportType.FBXIT_SKELETAL_MESH).items():
    options.set_editor_property(key, value)
data = options.get_editor_property("skeletal_mesh_import_data")
for key, value in dict(import_morph_targets=True, import_meshes_in_bone_hierarchy=True,
                       convert_scene=True, convert_scene_unit=True, import_uniform_scale=1.0,
                       update_skeleton_reference_pose=False, use_t0_as_ref_pose=False,
                       normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS).items():
    data.set_editor_property(key, value)
mesh = next(a for a in import_asset(ROOT / "Exports/Gratia/GameRig/Gratia_Game_reference_preview.fbx",
                                   "SK_Gratia_Game", options) if isinstance(a, unreal.SkeletalMesh))
assert mesh.get_editor_property("skeleton") == skeleton, "Existing skeleton was replaced"
assert bones(mesh) == original_bones, "Bone order/name changed during mesh reimport"
new_slots = [str(s.get_editor_property("imported_material_slot_name")) for s in mesh.get_editor_property("materials")]
old_slots = [str(s.get_editor_property("imported_material_slot_name")) for s in materials]
assert new_slots == old_slots, ("Material slot order changed", old_slots, new_slots)
mesh.set_editor_property("materials", materials)
mesh.set_editor_property("physics_asset", physics)
morphs = {m.get_name() for m in mesh.get_editor_property("morph_targets")}
assert old_morphs.issubset(morphs), sorted(old_morphs - morphs)
assert morphs == set(report["nonzero_morph_names"]), sorted(morphs.symmetric_difference(report["nonzero_morph_names"]))
assert lib.save_loaded_asset(mesh, only_if_is_dirty=False)

animation_reports = []
clips = {}
for clip in report["clips"]:
    name = "A_Gratia_Game_" + clip["clip"]
    options = unreal.FbxImportUI()
    for key, value in dict(import_mesh=False, import_animations=True, import_materials=False,
                           import_textures=False, automated_import_should_detect_type=False,
                           skeleton=skeleton, mesh_type_to_import=unreal.FBXImportType.FBXIT_ANIMATION,
                           original_import_type=unreal.FBXImportType.FBXIT_ANIMATION,
                           override_animation_name=name).items():
        options.set_editor_property(key, value)
    data = options.get_editor_property("anim_sequence_import_data")
    for key, value in dict(convert_scene=True, convert_scene_unit=True, use_default_sample_rate=True,
                           import_custom_attribute=False,
                           animation_length=unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME).items():
        data.set_editor_property(key, value)
    anim = next(a for a in import_asset(Path(clip["fbx"]), name, options) if isinstance(a, unreal.AnimSequence))
    assert anim.get_editor_property("skeleton") == skeleton
    assert abs(anim.get_play_length() - clip["duration_seconds"]) < 0.04
    curve_names = unreal.AnimationLibrary.get_animation_curve_names(anim, unreal.RawCurveTrackTypes.RCT_FLOAT)
    curve_ranges = {}
    for curve_name in curve_names:
        times, values = unreal.AnimationLibrary.get_float_keys(anim, curve_name)
        if values:
            curve_ranges[str(curve_name)] = {"min": min(values), "max": max(values), "keys": len(values)}
    assert any(c in curve_ranges and curve_ranges[c]["max"] > 0.05 for c in ("Mouth smile", "Eye L close", "Eye R close")), "No authored facial motion: " + name
    if clip["clip"] in ("ReactHand", "ReactCheer"):
        prefix = "Game_HandRef_" if clip["clip"] == "ReactHand" else "Game_CheerRef_"
        signed = {k: v for k, v in curve_ranges.items() if k.startswith(prefix)}
        assert signed and any(v["min"] < -0.01 for v in signed.values()), "Negative corrective coefficients were lost: " + name
    animation_reports.append(dict(clip=clip["clip"], asset=anim.get_path_name(),
                                  duration=anim.get_play_length(), curves=curve_ranges))
    clips[clip["clip"]] = anim

# Apply only the new routing/count/curve-ownership fields; keep artist/profile settings.
routing = {"Default": clips["ReactFace"]}
for zone in profile.get_editor_property("contact_zones"):
    name = str(zone.get_editor_property("name"))
    if name in ("Face", "Hair"):
        cue = "ReactFace"
    elif "hand" in name.lower() or "forearm" in name.lower() or "shoulder" in name.lower():
        cue = "ReactHand"
    else:
        cue = "ReactCheer"
    routing[name] = clips[cue]
profile.set_editor_property("reaction_clips", routing)
profile.set_editor_property("authored_reaction_facial_curves", True)
profile.set_editor_property("expected_morph_count", len(morphs))
validation = profile.validate_profile()
assert validation is not None, "Profile validation failed"
errors = validation[0] if len(validation) == 2 else validation[1]
assert not errors, list(errors)
assert lib.save_loaded_asset(profile, only_if_is_dirty=False)
lib.save_directory(directory, only_if_is_dirty=True, recursive=True)
output = dict(mesh=mesh.get_path_name(), skeleton=skeleton.get_path_name(), physics=physics.get_path_name(),
              bone_count=len(original_bones), morph_count=len(morphs), morphs=sorted(morphs),
              materials=old_slots, clips=animation_reports,
              routing={k: v.get_path_name() for k, v in routing.items()},
              blender_validation_sha256=hashlib.sha256(REPORT_PATH.read_bytes()).hexdigest())
(OUT / "reference_reactions_import.json").write_text(json.dumps(output, indent=2), encoding="utf-8")
unreal.log("GRATIA_REFERENCE_REACTIONS_IMPORTED " + json.dumps({k: output[k] for k in ("bone_count", "morph_count", "skeleton", "physics")}))
