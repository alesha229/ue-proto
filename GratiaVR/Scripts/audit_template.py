import json
from pathlib import Path
import unreal

out = Path(r'E:\coding\ue proto\evidence\01')
out.mkdir(parents=True, exist_ok=True)
assets = unreal.EditorAssetLibrary.list_assets('/Game/XRFramework', recursive=True, include_folder=False)
pawn_class = unreal.EditorAssetLibrary.load_blueprint_class('/Game/XRFramework/Blueprints/BP_XRPawn')
game_class = unreal.EditorAssetLibrary.load_blueprint_class('/Game/XRFramework/Blueprints/BP_XRGameMode')
assert pawn_class and game_class, 'VR framework classes failed to load'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
pawn = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(pawn_class, unreal.Vector(0,0,0))
report = {'assets': list(assets), 'pawn_class': str(pawn_class), 'game_class': str(game_class),
          'pawn_api': [n for n in dir(pawn) if any(w in n.lower() for w in ('calibr','height','reset','track','hand','menu','recenter'))],
          'components': []}
for c in pawn.get_components_by_class(unreal.ActorComponent):
    item = {'name':c.get_name(), 'class':c.get_class().get_name()}
    if isinstance(c, unreal.MotionControllerComponent):
        item['motion_source'] = str(c.get_editor_property('motion_source'))
    report['components'].append(item)
unreal.get_editor_subsystem(unreal.EditorActorSubsystem).destroy_actor(pawn)
(out / 'template_audit.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('GRATIA_TEMPLATE_AUDIT_OK')

