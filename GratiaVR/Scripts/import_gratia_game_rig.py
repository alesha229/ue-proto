"""Import the MCP-validated clean skeleton into separate assets."""
import json
from pathlib import Path
import unreal
ROOT=Path('E:/coding/ue proto')
OUT=ROOT/'evidence/03'
lib=unreal.EditorAssetLibrary
tools=unreal.AssetToolsHelpers.get_asset_tools()
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Gratia/Maps/L_Stage1')
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world,'Interchange.FeatureFlags.Import.FBX 0')
manifest=json.loads((ROOT/'Exports/Gratia/GameRig/game_rig_manifest.json').read_text(encoding='utf-8'))
assert manifest['runtime_approved']
old=lib.load_asset('/Game/Gratia/Character/SK_Gratia')
slots={str(s.get_editor_property('imported_material_slot_name')):s.get_editor_property('material_interface') for s in old.get_editor_property('materials')}
old_morphs={m.get_name() for m in old.get_editor_property('morph_targets')}

def run(file,name,options):
    task=unreal.AssetImportTask()
    for k,v in {'filename':str(file),'destination_path':'/Game/Gratia/GameRig','destination_name':name,'automated':True,'replace_existing':True,'save':True,'factory':unreal.FbxFactory(),'options':options}.items():
        task.set_editor_property(k,v)
    tools.import_asset_tasks([task])
    return [lib.load_asset(p) for p in task.get_editor_property('imported_object_paths')]

options=unreal.FbxImportUI()
for k,v in {'import_mesh':True,'import_as_skeletal':True,'import_animations':False,'import_materials':False,'import_textures':False,'create_physics_asset':False,'automated_import_should_detect_type':False,'mesh_type_to_import':unreal.FBXImportType.FBXIT_SKELETAL_MESH}.items():
    options.set_editor_property(k,v)
data=options.get_editor_property('skeletal_mesh_import_data')
for k,v in {'import_morph_targets':True,'import_meshes_in_bone_hierarchy':True,'convert_scene':True,'convert_scene_unit':True,'import_uniform_scale':1.0,'normal_import_method':unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS}.items():
    data.set_editor_property(k,v)
objects=run(ROOT/'Exports/Gratia/GameRig/Gratia_Game_preview.fbx','SK_Gratia_Game',options)
mesh=next(o for o in objects if isinstance(o,unreal.SkeletalMesh))
skeleton=mesh.get_editor_property('skeleton')
assert skeleton!=old.get_editor_property('skeleton')
material_slots=list(mesh.get_editor_property('materials'))
for slot in material_slots:
    label=str(slot.get_editor_property('imported_material_slot_name'))
    assert label in slots, label
    slot.set_editor_property('material_interface',slots[label])
mesh.set_editor_property('materials',material_slots)
morphs={m.get_name() for m in mesh.get_editor_property('morph_targets')}
assert old_morphs.issubset(morphs), old_morphs-morphs
assert len([n for n in morphs if n.startswith('Game_')])>=8, morphs
assert lib.save_loaded_asset(mesh)
report={'mesh':mesh.get_path_name(),'skeleton':skeleton.get_path_name(),'morph_count':len(morphs),'morph_names':sorted(morphs),'clips':[]}
for clip,length in [('Idle',6.0),('TestArms',4.0),('TestHead',4.0)]:
    options=unreal.FbxImportUI()
    name='A_Gratia_Game_'+clip
    for k,v in {'import_mesh':False,'import_animations':True,'import_materials':False,'import_textures':False,'automated_import_should_detect_type':False,'skeleton':skeleton,'mesh_type_to_import':unreal.FBXImportType.FBXIT_ANIMATION,'original_import_type':unreal.FBXImportType.FBXIT_ANIMATION,'override_animation_name':name}.items():
        options.set_editor_property(k,v)
    data=options.get_editor_property('anim_sequence_import_data')
    for k,v in {'convert_scene':True,'convert_scene_unit':True,'use_default_sample_rate':True,'import_custom_attribute':False,'animation_length':unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME}.items():data.set_editor_property(k,v)
    anim=next(o for o in run(ROOT/'Exports/Gratia/GameRig'/('Gratia_Game_'+clip+'.fbx'),name,options) if isinstance(o,unreal.AnimSequence))
    assert abs(anim.get_play_length()-length)<.04
    assert anim.get_editor_property('skeleton')==skeleton
    report['clips'].append({'name':clip,'asset':anim.get_path_name(),'duration':anim.get_play_length()})
lib.save_directory('/Game/Gratia/GameRig',only_if_is_dirty=True,recursive=True)
(OUT/'game_rig_import_manifest.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
unreal.log('GRATIA_CLEAN_GAME_RIG_IMPORTED')
