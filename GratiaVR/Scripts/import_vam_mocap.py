"""Import the MCP-validated KM466 performance clip (author_vam_mocap.py) into Unreal.

GRATIA_MOCAP_LONG=1: the whole take, one clip per validated 60 s segment, listed as one
segmented performance (Clip = first segment, Segments = the rest).

Editor commandlet after the C++ editor build. Reimports SK_Gratia_Game from the clip FBX for its
new Game_KM466_* corrective morph targets while keeping the skeleton, bone order, material slots
and physics asset, imports A_Gratia_Game_KM466 and lists it in DA_Gratia.PerformanceClips.
"""
import hashlib
import json
import os
import shutil
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[2]
LONG = os.environ.get("GRATIA_MOCAP_LONG") == "1"
EVIDENCE = ROOT / ("evidence/05/kitty_mocap_full" if LONG else "evidence/05/kitty_mocap_v3")
CLIP = "KM466Full" if LONG else "KM466"
LABEL = "KM466 Full" if LONG else "KM466"
if LONG:
    report_paths = sorted(EVIDENCE.glob("km466full_S*_validation.json"))
    basis_json = EVIDENCE / "km466full_basis_fit.json"
    basis = json.loads((basis_json if basis_json.exists() else EVIDENCE / "km466full_basis.json").read_text(encoding="utf-8"))
    corrective_prefix, corrective_count = "Game_%s_" % CLIP, basis["rank"]
else:
    report_paths = [EVIDENCE / "km466_validation.json"]
reports = [json.loads(p.read_text(encoding="utf-8")) for p in report_paths]
assert reports, "no validated clip reports"
for report in reports:
    assert (report["passed"] or report.get("allowed_failed")) and report.get("exported"), \
        "Blender validation must pass and the FBX must be exported: " + str(report.get("segment"))
    fbx = Path(report["fbx"])
    assert fbx.is_file() and fbx.stat().st_size == report["bytes"], str(fbx)
    assert hashlib.sha256(fbx.read_bytes()).hexdigest() == report["sha256"], "FBX differs from the validated export: " + str(fbx)
if not LONG:
    corrective_prefix = reports[0]["corrective"]["prefix"]
    corrective_count = reports[0]["corrective"]["rank"]
report = reports[0]
fbx = Path(report["fbx"])

lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
directory = "/Game/Gratia/GameRig"
mesh = lib.load_asset(directory + "/SK_Gratia_Game")
profile = lib.load_asset("/Game/Characters/Profiles/DA_Gratia")
assert isinstance(mesh, unreal.SkeletalMesh) and isinstance(profile, unreal.GratiaCharacterProfile)
skeleton = mesh.get_editor_property("skeleton")
physics = mesh.get_editor_property("physics_asset")
materials = list(mesh.get_editor_property("materials"))
old_morphs = {m.get_name().lower() for m in mesh.get_editor_property("morph_targets")}  # FName case is not stable


def bones(asset):
    component = unreal.SkeletalMeshComponent()
    component.set_skinned_asset_and_update(asset)
    return [str(component.get_bone_name(i)) for i in range(component.get_num_bones())]


def mesh_settings(asset):
    physics_asset = asset.get_editor_property("physics_asset")
    lods = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem).get_lod_count(asset)
    return {"sockets": asset.num_sockets(), "lods": lods,
            "physics": physics_asset.get_path_name() if physics_asset else None}


original_bones = bones(mesh)
settings_before = mesh_settings(mesh)
backup = EVIDENCE / "before_unreal_import"
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
mesh = next(a for a in import_asset(fbx, "SK_Gratia_Game", options) if isinstance(a, unreal.SkeletalMesh))
assert mesh.get_editor_property("skeleton") == skeleton, "Existing skeleton was replaced"
assert bones(mesh) == original_bones, "Bone order/name changed during mesh reimport"
new_slots = [str(s.get_editor_property("imported_material_slot_name")) for s in mesh.get_editor_property("materials")]
old_slots = [str(s.get_editor_property("imported_material_slot_name")) for s in materials]
assert new_slots == old_slots, ("Material slot order changed", old_slots, new_slots)
mesh.set_editor_property("materials", materials)
mesh.set_editor_property("physics_asset", physics)
morphs = {m.get_name().lower() for m in mesh.get_editor_property("morph_targets")}
assert old_morphs.issubset(morphs), sorted(old_morphs - morphs)
wanted = {("%s%d" % (corrective_prefix, k)).lower() for k in range(1, corrective_count + 1)}
assert wanted.issubset(morphs), sorted(wanted - morphs)
settings_after = mesh_settings(mesh)
assert settings_after == settings_before, ("Mesh settings changed", settings_before, settings_after)
assert lib.save_loaded_asset(mesh, only_if_is_dirty=False)

def import_clip(path, name, expected_seconds):
    options = unreal.FbxImportUI()
    for key, value in dict(import_mesh=False, import_animations=True, import_materials=False,
                           import_textures=False, automated_import_should_detect_type=False,
                           skeleton=skeleton, mesh_type_to_import=unreal.FBXImportType.FBXIT_ANIMATION,
                           original_import_type=unreal.FBXImportType.FBXIT_ANIMATION,
                           override_animation_name=name).items():
        options.set_editor_property(key, value)
    data = options.get_editor_property("anim_sequence_import_data")
    for key, value in dict(convert_scene=True, convert_scene_unit=True, use_default_sample_rate=False,
                           import_custom_attribute=False,
                           animation_length=unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME).items():
        data.set_editor_property(key, value)
    anim = next(a for a in import_asset(path, name, options) if isinstance(a, unreal.AnimSequence))
    assert anim.get_editor_property("skeleton") == skeleton
    assert abs(anim.get_play_length() - expected_seconds) < 0.04, (name, anim.get_play_length(), expected_seconds)
    ranges = {}
    for curve_name in unreal.AnimationLibrary.get_animation_curve_names(anim, unreal.RawCurveTrackTypes.RCT_FLOAT):
        times, values = unreal.AnimationLibrary.get_float_keys(anim, curve_name)
        if values:
            ranges[str(curve_name).lower()] = {"min": min(values), "max": max(values), "keys": len(values)}
    correctives = {k: v for k, v in ranges.items() if k.startswith(corrective_prefix.lower())}
    assert len(correctives) == corrective_count, (name, sorted(correctives))
    # Shape keys with no offset are not imported as morphs; their constant curves are ignored at runtime.
    driven = [k for k in ranges if k not in morphs and (k.startswith(corrective_prefix.lower()) or k in ("eye l close", "eye r close", "mouth smile"))]
    assert not driven, ("Clip curves without morph targets", name, driven)
    return anim, ranges


anims, curve_ranges, missing = [], {}, []
for number, part in enumerate(reports, 1):
    seconds = (part["frames"][1] - part["frames"][0]) / 30.0 if LONG else part["duration_seconds"]
    name = "A_Gratia_Game_%s_S%02d" % (CLIP, number) if LONG else "A_Gratia_Game_" + CLIP
    anim, ranges = import_clip(Path(part["fbx"]), name, seconds)
    anims.append(anim)
    for k, v in ranges.items():
        merged = curve_ranges.setdefault(k, {"min": v["min"], "max": v["max"], "keys": 0})
        merged["min"], merged["max"], merged["keys"] = min(merged["min"], v["min"]), max(merged["max"], v["max"]), merged["keys"] + v["keys"]
missing = sorted(k for k in curve_ranges if k not in morphs)
correctives = {k: v for k, v in curve_ranges.items() if k.startswith(corrective_prefix.lower())}
assert any(v["min"] < -0.01 for v in correctives.values()), "Negative corrective coefficients were lost"
assert max(v["max"] for v in correctives.values()) > 0.9, "Corrective coefficients lost their range"
assert curve_ranges.get("eye l close", {}).get("max", 0) > 0.5, "Authored blinks are missing"
anim = anims[0]

entry = unreal.GratiaPerformanceClip()
entry.set_editor_property("name", LABEL)
entry.set_editor_property("clip", anims[0])
entry.set_editor_property("segments", anims[1:])
entry.set_editor_property("loop", True)
clips = [c for c in profile.get_editor_property("performance_clips") if str(c.get_editor_property("name")) != LABEL] + [entry]
profile.set_editor_property("performance_clips", clips)
profile.set_editor_property("expected_morph_count", len(morphs))
validation = profile.validate_profile()
assert validation is not None, "Profile validation failed"
errors = validation[0] if len(validation) == 2 else validation[1]
assert not errors, list(errors)
assert lib.save_loaded_asset(profile, only_if_is_dirty=False)
lib.save_directory(directory, only_if_is_dirty=True, recursive=True)
output = dict(mesh=mesh.get_path_name(), animation=anim.get_path_name(), duration=sum(a.get_play_length() for a in anims),
              segments=[a.get_path_name() for a in anims],
              bone_count=len(original_bones), morph_count=len(morphs), added_morphs=sorted(morphs - old_morphs),
              materials=old_slots, mesh_settings=settings_after, curves=curve_ranges, curves_without_morph=missing,
              performance_clips=[str(c.get_editor_property("name")) for c in clips],
              blender_validation_sha256=[hashlib.sha256(p.read_bytes()).hexdigest() for p in report_paths],
              fbx_sha256=[r["sha256"] for r in reports])
(EVIDENCE / ("%s_unreal_import.json" % CLIP.lower())).write_text(json.dumps(output, indent=2), encoding="utf-8")
unreal.log("GRATIA_KM466_IMPORTED " + json.dumps({k: output[k] for k in ("animation", "duration", "bone_count", "morph_count")}))
