"""Reimport SK_Gratia_Game from the mesh-only game-rig FBX (author_soft_regions.py PHASE 'export').

Editor commandlet. Keeps the skeleton (new soft bones are merged into it), material slots, physics
asset and every existing morph target; reports added bones and morphs. Clips are not touched.
"""
import hashlib
import json
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[2]
FBX = ROOT / "Exports/Gratia/GameRig/Gratia_Game_mesh.fbx"
EVIDENCE = ROOT / "evidence/05/soft_regions"
export = json.loads((EVIDENCE / "soft_regions_export.json").read_text(encoding="utf-8"))
assert FBX.is_file() and hashlib.sha256(FBX.read_bytes()).hexdigest() == export["sha256"], "FBX differs from the Blender export"

lib = unreal.EditorAssetLibrary
directory = "/Game/Gratia/GameRig"
mesh = lib.load_asset(directory + "/SK_Gratia_Game")
skeleton = mesh.get_editor_property("skeleton")
physics = mesh.get_editor_property("physics_asset")
materials = list(mesh.get_editor_property("materials"))
old_morphs = {m.get_name().lower() for m in mesh.get_editor_property("morph_targets")}


def bones(asset):
    component = unreal.SkeletalMeshComponent()
    component.set_skinned_asset_and_update(asset)
    return [str(component.get_bone_name(i)) for i in range(component.get_num_bones())]


old_bones = bones(mesh)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level("/Game/Gratia/Maps/L_Stage1")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, "Interchange.FeatureFlags.Import.FBX 0")
options = unreal.FbxImportUI()
for key, value in dict(import_mesh=True, import_as_skeletal=True, import_animations=False, import_materials=False,
                       import_textures=False, create_physics_asset=False, skeleton=skeleton, physics_asset=physics,
                       automated_import_should_detect_type=False, mesh_type_to_import=unreal.FBXImportType.FBXIT_SKELETAL_MESH).items():
    options.set_editor_property(key, value)
data = options.get_editor_property("skeletal_mesh_import_data")
for key, value in dict(import_morph_targets=True, import_meshes_in_bone_hierarchy=True, convert_scene=True, convert_scene_unit=True,
                       import_uniform_scale=1.0, update_skeleton_reference_pose=False, use_t0_as_ref_pose=False,
                       normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS).items():
    data.set_editor_property(key, value)
task = unreal.AssetImportTask()
for key, value in dict(filename=str(FBX), destination_path=directory, destination_name="SK_Gratia_Game", automated=True,
                       replace_existing=True, replace_existing_settings=True, save=True, factory=unreal.FbxFactory(), options=options).items():
    task.set_editor_property(key, value)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
mesh = lib.load_asset(directory + "/SK_Gratia_Game")
assert mesh.get_editor_property("skeleton") == skeleton, "existing skeleton was replaced"
new_bones = bones(mesh)
assert all(b in new_bones for b in old_bones), sorted(set(old_bones) - set(new_bones))
added_bones = [b for b in new_bones if b not in old_bones]
new_slots = [str(s.get_editor_property("imported_material_slot_name")) for s in mesh.get_editor_property("materials")]
old_slots = [str(s.get_editor_property("imported_material_slot_name")) for s in materials]
assert new_slots == old_slots, ("material slot order changed", old_slots, new_slots)
mesh.set_editor_property("materials", materials)
mesh.set_editor_property("physics_asset", physics)
morphs = {m.get_name().lower() for m in mesh.get_editor_property("morph_targets")}
assert old_morphs.issubset(morphs), sorted(old_morphs - morphs)
assert lib.save_loaded_asset(mesh, only_if_is_dirty=False)
lib.save_loaded_asset(skeleton, only_if_is_dirty=False)
# Regression counts of the profile follow the new mesh (added soft bones).
profile = lib.load_asset("/Game/Characters/Profiles/DA_Gratia")
profile.set_editor_property("expected_bone_count", len(new_bones))
profile.set_editor_property("expected_morph_count", len(morphs))
validation = profile.validate_profile()
errors = validation[0] if len(validation) == 2 else validation[1]
assert not errors, list(errors)
assert lib.save_loaded_asset(profile, only_if_is_dirty=False)
result = {"fbx_sha256": export["sha256"], "bones": len(new_bones), "added_bones": added_bones, "morphs": len(morphs),
          "added_morphs": sorted(morphs - old_morphs), "materials": old_slots}
(EVIDENCE / "unreal_mesh_reimport.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
unreal.log("GRATIA_MESH_REIMPORTED " + json.dumps({k: result[k] for k in ("bones", "added_bones", "morphs")}))
