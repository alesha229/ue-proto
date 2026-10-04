"""Reimport the aliased morph and import FK clips; place in a fresh process afterward."""
import json
from pathlib import Path
import unreal

ROOT = Path(r'E:\coding\ue proto')
OUT = ROOT / 'evidence/02'
assets = unreal.AssetToolsHelpers.get_asset_tools()
library = unreal.EditorAssetLibrary
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert levels.load_level('/Game/Gratia/Maps/L_Stage1')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, 'Interchange.FeatureFlags.Import.FBX 0')
mesh = library.load_asset('/Game/Gratia/Character/SK_Gratia')
skeleton = mesh.get_editor_property('skeleton')
old_materials = {str(slot.get_editor_property('imported_material_slot_name')): slot.get_editor_property('material_interface')
                 for slot in mesh.get_editor_property('materials')}
for material in set(old_materials.values()):
    if isinstance(material, unreal.Material) and material.get_path_name().startswith('/Game/Gratia/CharacterMaterials/'):
        material.set_editor_property('used_with_skeletal_mesh', True)
        material.set_editor_property('used_with_morph_targets', True)
        assert not unreal.MaterialEditingLibrary.recompile_material(material)
        assert library.save_loaded_asset(material)
options = unreal.FbxImportUI()
for key, value in {'import_mesh': True, 'import_as_skeletal': True, 'import_animations': False,
    'import_materials': False, 'import_textures': False, 'create_physics_asset': False,
    'automated_import_should_detect_type': False, 'skeleton': skeleton,
    'mesh_type_to_import': unreal.FBXImportType.FBXIT_SKELETAL_MESH}.items():
    options.set_editor_property(key, value)
mesh_options = options.get_editor_property('skeletal_mesh_import_data')
for key, value in {'import_morph_targets': True, 'import_meshes_in_bone_hierarchy': True,
    'convert_scene': True, 'convert_scene_unit': True, 'import_uniform_scale': 1.0,
    'update_skeleton_reference_pose': False, 'use_t0_as_ref_pose': False,
    'normal_import_method': unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS}.items():
    mesh_options.set_editor_property(key, value)

def import_fbx(file, folder, name, import_options):
    task = unreal.AssetImportTask()
    for key, value in {'filename': str(file), 'destination_path': folder, 'destination_name': name,
        'automated': True, 'replace_existing': True, 'save': True, 'factory': unreal.FbxFactory(),
        'options': import_options}.items():
        task.set_editor_property(key, value)
    assets.import_asset_tasks([task])
    paths = list(task.get_editor_property('imported_object_paths'))
    assert paths, 'FBX import returned no assets: ' + str(file)
    return [library.load_asset(path) for path in paths]

imported = import_fbx(ROOT / 'Exports/Gratia/Gratia_preview.fbx', '/Game/Gratia/Character', 'SK_Gratia', options)
assert any(isinstance(obj, unreal.SkeletalMesh) for obj in imported)
mesh = library.load_asset('/Game/Gratia/Character/SK_Gratia')
slots = list(mesh.get_editor_property('materials'))
for slot in slots:
    label = str(slot.get_editor_property('imported_material_slot_name'))
    if label in old_materials:
        slot.set_editor_property('material_interface', old_materials[label])
mesh.set_editor_property('materials', slots)
library.save_loaded_asset(mesh)
morphs = [m.get_name() for m in mesh.get_editor_property('morph_targets')]
expected = [name for names in json.loads((OUT / 'export_manifest.json').read_text(encoding='utf-8'))['morphs'].values()
            for name in names if name != 'Horny']
assert len(morphs) == 50 and set(morphs) == set(expected), {'actual': morphs, 'expected': expected}
assert mesh.get_editor_property('skeleton') == skeleton, 'Skeleton changed during morph reimport'

animations = {}
report = {'mesh': mesh.get_path_name(), 'morph_count': len(morphs), 'morph_names': morphs, 'animations': []}
for clip, expected_duration in (('Idle', 6.0), ('TestArms', 4.0), ('TestHead', 4.0)):
    anim_options = unreal.FbxImportUI()
    name = 'A_Gratia_' + clip
    for key, value in {'import_mesh': False, 'import_animations': True, 'import_materials': False,
        'import_textures': False, 'automated_import_should_detect_type': False, 'skeleton': skeleton,
        'mesh_type_to_import': unreal.FBXImportType.FBXIT_ANIMATION,
        'original_import_type': unreal.FBXImportType.FBXIT_ANIMATION,
        'override_animation_name': name}.items():
        anim_options.set_editor_property(key, value)
    data = anim_options.get_editor_property('anim_sequence_import_data')
    for key, value in {'convert_scene': True, 'convert_scene_unit': True,
        'use_default_sample_rate': True, 'import_custom_attribute': False,
        'animation_length': unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME}.items():
        data.set_editor_property(key, value)
    objects = import_fbx(ROOT / 'Exports/Gratia' / ('Gratia_' + clip + '.fbx'), '/Game/Gratia/Animations', name, anim_options)
    sequences = [obj for obj in objects if isinstance(obj, unreal.AnimSequence)]
    assert len(sequences) == 1, objects
    sequence = sequences[0]
    assert sequence.get_path_name() == '/Game/Gratia/Animations/' + name + '.' + name, sequence.get_path_name()
    assert sequence.get_editor_property('skeleton') == skeleton
    duration = sequence.get_play_length()
    assert abs(duration - expected_duration) < 0.04, (clip, duration)
    animations[clip] = sequence
    report['animations'].append({'clip': clip, 'asset': sequence.get_path_name(), 'duration_seconds': duration})

library.save_directory('/Game/Gratia', only_if_is_dirty=True, recursive=True)
(OUT / 'animation_import_manifest.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
# The comparison script reads this manifest's imported morph names.
original = json.loads((OUT / 'unreal_import_manifest.json').read_text(encoding='utf-8'))
original.update({'morph_count': len(morphs), 'morph_names': morphs,
    'status': 'Aliased mouth morph restored; natural idle and two deformation test clips imported. Headset acceptance remains open.'})
(OUT / 'unreal_import_manifest.json').write_text(json.dumps(original, indent=2), encoding='utf-8')
unreal.log('GRATIA_ANIMATION_IMPORTED')
