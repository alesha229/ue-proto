"""Place the animated actor in a fresh Unreal process after clips have been saved."""
import json
from pathlib import Path
import unreal

ROOT = Path(r'E:\coding\ue proto')
OUT = ROOT / 'evidence/02'
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert levels.load_level('/Game/Gratia/Maps/L_Stage1')
mesh = unreal.load_asset('/Game/Gratia/Character/SK_Gratia')
idle = unreal.load_asset('/Game/Gratia/Animations/A_Gratia_Idle')
assert mesh and idle
old_actors = [actor for actor in actors.get_all_level_actors() if actor.get_actor_label() == 'Gratia_Preview']
assert len(old_actors) == 1, old_actors
old_actor = old_actors[0]
transform = old_actor.get_actor_transform()
preview_class = unreal.load_class(None, '/Script/GratiaVR.GratiaPreviewCharacter')
assert preview_class
new_actor = actors.spawn_actor_from_class(preview_class, transform.translation, transform.rotation.rotator())
new_actor.set_actor_transform(transform, False, True)
new_actor.set_actor_label('Gratia_Preview')
component = new_actor.get_component_by_class(unreal.SkeletalMeshComponent)
component.set_skinned_asset_and_update(mesh)
component.override_animation_data(idle, True, True, 0.0, 1.0)
component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
component.set_editor_property('cast_shadow', False)
bones = [str(component.get_bone_name(index)) for index in range(component.get_num_bones())]
assert all(name in bones for name in ('root', 'DEF-spine_006', 'DEF-hand_L', 'DEF-hand_R', 'DEF-foot_L', 'DEF-foot_R'))
assert actors.destroy_actor(old_actor)
assert levels.save_current_level()
report = json.loads((OUT / 'animation_import_manifest.json').read_text(encoding='utf-8'))
report.update({'bone_count': len(bones), 'bone_names': bones, 'actor': new_actor.get_path_name(), 'actor_transform': str(transform)})
(OUT / 'animation_import_manifest.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
original = json.loads((OUT / 'unreal_import_manifest.json').read_text(encoding='utf-8'))
original['actor'] = new_actor.get_path_name()
(OUT / 'unreal_import_manifest.json').write_text(json.dumps(original, indent=2), encoding='utf-8')
unreal.log('GRATIA_ANIMATED_PREVIEW_PLACED')
