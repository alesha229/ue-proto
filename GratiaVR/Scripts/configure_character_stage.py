"""Connect existing stage actors explicitly to editable character/scene assets."""
import unreal

unreal.EditorLoadingAndSavingUtils.load_map('/Game/Gratia/Maps/L_Stage1')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
character = next(a for a in actors if isinstance(a, unreal.GratiaPreviewCharacter) and a.get_actor_label() == 'Gratia_Preview')
runtime = next(a for a in actors if isinstance(a, unreal.GratiaStage1Runtime) and a.get_actor_label() == 'Stage1CalibrationAndTrackingGuard')
cube = next(a for a in actors if a.get_actor_label() == 'ScaleCube_Exactly1m')
profile = unreal.load_asset('/Game/Characters/Profiles/DA_Gratia')
assert profile and profile.get_editor_property('mesh'), 'Generate profiles before configuring stage'
character.set_editor_property('character_profile', profile)
character.get_editor_property('character_mesh').set_skinned_asset_and_update(profile.get_editor_property('mesh'))
runtime.set_editor_property('target_character', character)
runtime.set_editor_property('scene_contact_actor', cube)
cube.set_editor_property('tags', list(set(list(cube.get_editor_property('tags')) + [unreal.Name('GratiaSceneContact')])) )
unreal.EditorLoadingAndSavingUtils.save_current_level()
unreal.log('GRATIA_STAGE_PROFILE_CONFIGURED')
