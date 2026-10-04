"""Persist the clean mesh/clip on the existing map character without changing its transform."""
import json
from pathlib import Path
import unreal
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert levels.load_level('/Game/Gratia/Maps/L_Stage1')
mesh=unreal.load_asset('/Game/Gratia/GameRig/SK_Gratia_Game')
idle=unreal.load_asset('/Game/Gratia/GameRig/A_Gratia_Game_Idle')
character=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='Gratia_Preview')
component=character.get_component_by_class(unreal.SkeletalMeshComponent)
transform=str(character.get_actor_transform())
component.set_skinned_asset_and_update(mesh)
component.override_animation_data(idle,True,True,0.0,1.0)
names=[str(component.get_bone_name(i)) for i in range(component.get_num_bones())]
assert 245<=len(names)<=246, len(names)
assert not any(n.startswith(('ORG-','MCH-')) for n in names)
assert levels.save_current_level()
for actor in actors.get_all_level_actors():
    if actor.get_actor_label()=='Stage1Instructions':
        actor.get_component_by_class(unreal.TextRenderComponent).set_text('GRATIA VR\nLeft stick: walk | Right stick: turn\nY/B: settings | F1: debug\nR: recenter | F4: settings')
assert levels.save_current_level()
Path('E:/coding/ue proto/evidence/03/game_rig_placement.json').write_text(json.dumps({'mesh':mesh.get_path_name(),'actor':character.get_path_name(),'transform':transform,'bones':names,'bone_count':len(names)},indent=2),encoding='utf-8')
unreal.log('GRATIA_GAME_RIG_PLACED')
