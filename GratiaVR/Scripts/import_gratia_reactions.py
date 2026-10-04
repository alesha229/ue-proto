"""Import independently validated two-second cues without changing the mesh."""
import json
from pathlib import Path
import unreal
root=Path("E:/coding/ue proto")
lib=unreal.EditorAssetLibrary
mesh=lib.load_asset("/Game/Gratia/GameRig/SK_Gratia_Game")
skeleton=mesh.get_editor_property("skeleton")
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
levels.load_level("/Game/Gratia/Maps/L_Stage1")
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world,"Interchange.FeatureFlags.Import.FBX 0")
report=[]
for clip in ["ReactSoft","ReactBright"]:
    name="A_Gratia_Game_"+clip
    options=unreal.FbxImportUI()
    for key,value in {"import_mesh":False,"import_animations":True,"import_materials":False,"import_textures":False,
        "automated_import_should_detect_type":False,"skeleton":skeleton,"mesh_type_to_import":unreal.FBXImportType.FBXIT_ANIMATION,
        "original_import_type":unreal.FBXImportType.FBXIT_ANIMATION,"override_animation_name":name}.items():
        options.set_editor_property(key,value)
    data=options.get_editor_property("anim_sequence_import_data")
    for key,value in {"convert_scene":True,"convert_scene_unit":True,"use_default_sample_rate":True,"import_custom_attribute":False,
        "animation_length":unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME}.items(): data.set_editor_property(key,value)
    task=unreal.AssetImportTask()
    for key,value in {"filename":str(root/"Exports/Gratia/GameRig"/("Gratia_Game_"+clip+".fbx")),
        "destination_path":"/Game/Gratia/GameRig","destination_name":name,"automated":True,"replace_existing":True,
        "save":True,"factory":unreal.FbxFactory(),"options":options}.items(): task.set_editor_property(key,value)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    anim=next(lib.load_asset(p) for p in task.get_editor_property("imported_object_paths") if isinstance(lib.load_asset(p),unreal.AnimSequence))
    assert abs(anim.get_play_length()-2.0)<0.04
    assert anim.get_editor_property("skeleton")==skeleton
    report.append({"asset":anim.get_path_name(),"duration":anim.get_play_length()})
(root/"evidence/03/reaction_import.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
unreal.log("GRATIA_REACTIONS_IMPORTED")
