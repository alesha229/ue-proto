"""Import the first rigged Gratia preview and place it in the test room."""
import json
import re
from pathlib import Path
import unreal

ROOT = Path(r'E:\coding\ue proto')
OUT = ROOT / 'evidence/02'
assets = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert levels.load_level('/Game/Gratia/Maps/L_Stage1')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, 'Interchange.FeatureFlags.Import.FBX 0')
options = unreal.FbxImportUI()
for key, value in {'import_mesh': True, 'import_as_skeletal': True, 'import_animations': False,
    'import_materials': False, 'import_textures': False, 'create_physics_asset': False,
    'automated_import_should_detect_type': False,
    'mesh_type_to_import': unreal.FBXImportType.FBXIT_SKELETAL_MESH}.items():
    options.set_editor_property(key, value)
mesh_options = options.get_editor_property('skeletal_mesh_import_data')
for key, value in {'import_morph_targets': True, 'import_meshes_in_bone_hierarchy': True,
    'convert_scene': True, 'convert_scene_unit': True, 'import_uniform_scale': 1.0,
    'normal_import_method': unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS}.items():
    mesh_options.set_editor_property(key, value)
task = unreal.AssetImportTask()
for key, value in {'filename': str(ROOT / 'Exports/Gratia/Gratia_preview.fbx'),
    'destination_path': '/Game/Gratia/Character', 'destination_name': 'SK_Gratia',
    'automated': True, 'replace_existing': True, 'save': True,
    'factory': unreal.FbxFactory(), 'options': options}.items():
    task.set_editor_property(key, value)
assets.import_asset_tasks([task])
imported = list(task.get_editor_property('imported_object_paths'))
meshes = [editor_assets.load_asset(path) for path in imported]
meshes = [m for m in meshes if isinstance(m, unreal.SkeletalMesh)]
assert len(meshes) == 1, f'Expected one combined skeletal mesh, got {imported}'
mesh = meshes[0]

texture_tasks = []
for file in sorted((ROOT / 'textures').glob('*.png')):
    item = unreal.AssetImportTask()
    for key, value in {'filename': str(file), 'destination_path': '/Game/Gratia/Textures',
        'destination_name': 'T_' + file.stem, 'automated': True, 'replace_existing': True, 'save': True}.items():
        item.set_editor_property(key, value)
    texture_tasks.append(item)
assets.import_asset_tasks(texture_tasks)
textures = {}
for item in texture_tasks:
    file = Path(item.get_editor_property('filename'))
    paths = list(item.get_editor_property('imported_object_paths'))
    assert paths, f'Texture import failed: {file}'
    texture = editor_assets.load_asset(paths[0])
    texture.set_editor_property('max_texture_size', 2048)
    is_normal = 'norm' in file.stem
    is_data = is_normal or any(word in file.stem for word in ('metal', 'spec', 'alpha', 'mask'))
    texture.set_editor_property('srgb', not is_data)
    if is_normal:
        texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        texture.set_editor_property('flip_green_channel', True)
    textures[file.stem] = texture
    editor_assets.save_loaded_asset(texture)

specs = {
    'Body_skin': ('body_diff', 'body_normals', None, None, False),
    'Nails': ('nails', None, None, None, False),
    'Default cloth 1': ('default_cloth1_diff', 'default_cloth1_norm', 'default_cloth1_metal', None, False),
    'Default cloth 2': ('default_cloth2_diff', 'default_cloth2_norm', 'default_cloth2_metal', 'default_cloth2_alpha', False),
    'Gratia ears': ('ears_diff', 'ears_norm', 'ears_metal', None, False),
    'Gratia_face': ('face_diff', None, None, None, True),
    'Gratia_brows': ('face_diff', None, None, 'diff_alpha', False),
    'Gratia_eyes': ('eyes_diff', None, None, None, True),
    'Gratia_eyeshadows': ('eyeshadows', None, None, 'diff_alpha', False),
    'Gratia_eye_detail': ('eyes_detail', None, None, 'diff_alpha', True),
    'Gratia_hair': ('hair_diff', 'hair_norm', None, None, False),
}
def normalize(value):
    return re.sub('[^a-z0-9]', '', str(value).lower())
edit = unreal.MaterialEditingLibrary
materials = {}
for label, spec in specs.items():
    diffuse, normal, metal, alpha, unlit = spec
    name = 'M_Gratia_' + re.sub('[^A-Za-z0-9_]', '_', label)
    path = '/Game/Gratia/CharacterMaterials/' + name
    material = editor_assets.load_asset(path) if editor_assets.does_asset_exist(path) else assets.create_asset(name, '/Game/Gratia/CharacterMaterials', unreal.Material, unreal.MaterialFactoryNew())
    edit.delete_all_material_expressions(material)
    # Editor auto-detection can enable usage without persisting it to the asset.
    # Cooked games cannot add shader permutations at runtime.
    material.set_editor_property('used_with_skeletal_mesh', True)
    material.set_editor_property('used_with_morph_targets', True)
    material.set_editor_property('two_sided', bool(alpha) or label == 'Gratia_hair')
    if alpha:
        material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
        material.set_editor_property('opacity_mask_clip_value', 0.2)
    if unlit:
        material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    def sample(key, sampler_type, ypos):
        node = edit.create_material_expression(material, unreal.MaterialExpressionTextureSample, -400, ypos)
        node.set_editor_property('texture', textures[key])
        node.set_editor_property('sampler_type', sampler_type)
        return node
    color = sample(diffuse, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 0)
    edit.connect_material_property(color, 'RGB', unreal.MaterialProperty.MP_EMISSIVE_COLOR if unlit else unreal.MaterialProperty.MP_BASE_COLOR)
    if normal:
        edit.connect_material_property(sample(normal, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, 250), 'RGB', unreal.MaterialProperty.MP_NORMAL)
    if metal:
        edit.connect_material_property(sample(metal, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, 500), 'R', unreal.MaterialProperty.MP_METALLIC)
    if alpha:
        alpha_node = color if alpha == 'diff_alpha' else sample(alpha, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, 750)
        if alpha == 'default_cloth2_alpha':
            invert = edit.create_material_expression(material, unreal.MaterialExpressionOneMinus, -100, 750)
            assert edit.connect_material_expressions(alpha_node, 'R', invert, 'None'), 'Clothing alpha inversion failed'
            edit.connect_material_property(invert, '', unreal.MaterialProperty.MP_OPACITY_MASK)
        else:
            edit.connect_material_property(alpha_node, 'A' if alpha == 'diff_alpha' else 'R', unreal.MaterialProperty.MP_OPACITY_MASK)
    rough = edit.create_material_expression(material, unreal.MaterialExpressionConstant, -400, 1000)
    rough.set_editor_property('r', 0.65)
    edit.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    edit.recompile_material(material)
    editor_assets.save_loaded_asset(material)
    materials[normalize(label)] = material
slots = list(mesh.get_editor_property('materials'))
assigned = []
for slot in slots:
    label = str(slot.get_editor_property('imported_material_slot_name'))
    material = materials.get(normalize(label))
    if material:
        slot.set_editor_property('material_interface', material)
    assigned.append({'slot': label, 'material': material.get_path_name() if material else 'engine fallback / outline'})
mesh.set_editor_property('materials', slots)
editor_assets.save_loaded_asset(mesh)
for actor in actors.get_all_level_actors():
    if actor.get_actor_label() == 'Gratia_Preview':
        actors.destroy_actor(actor)
character = actors.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(150,0,0), unreal.Rotator(pitch=0,yaw=90,roll=0))
character.set_actor_label('Gratia_Preview')
component = character.get_component_by_class(unreal.SkeletalMeshComponent)
component.set_skinned_asset_and_update(mesh)
component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
component.set_editor_property('cast_shadow', False)
origin, extent = character.get_actor_bounds(False)
character.add_actor_world_offset(unreal.Vector(0,0,-(origin.z-extent.z)), False, False)
assert levels.save_current_level()
editor_assets.save_directory('/Game/Gratia', only_if_is_dirty=True, recursive=True)
morphs = [m.get_name() for m in mesh.get_editor_property('morph_targets')]
report = {'imported': imported, 'mesh': mesh.get_path_name(), 'morph_count': len(morphs), 'morph_names': morphs,
    'materials': assigned, 'actor': character.get_path_name(), 'location_cm': list(character.get_actor_location().to_tuple()) if hasattr(character.get_actor_location(), 'to_tuple') else str(character.get_actor_location()),
    'bounds_extent_cm': {'x': extent.x, 'y': extent.y, 'z': extent.z},
    'status': 'First rigged textured preview. Material matching, animations, physical contacts and headset acceptance remain open.'}
(OUT / 'unreal_import_manifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('GRATIA_PREVIEW_IMPORTED_AND_PLACED')
