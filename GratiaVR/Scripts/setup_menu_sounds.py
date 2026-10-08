"""Menu interface sounds: copies of the engine's VR editor UI sounds inside the project, so they cook with the game.

Run as a commandlet (UnrealEditor-Cmd ... -run=pythonscript -script=<this file>). UGratiaMenu loads
/Game/Gratia/Audio/UI/S_MenuClick, S_MenuOpen and S_MenuClose. Report: evidence/04/menu_sounds.json.
"""
import json
from pathlib import Path
import unreal

ROOT = Path(__file__).resolve().parents[2]
DEST = '/Game/Gratia/Audio/UI'
SOUNDS = {
    'S_MenuClick': '/Engine/VREditor/Sounds/UI/Click_on_Button',
    'S_MenuOpen': '/Engine/VREditor/Sounds/UI/Floating_UI_Open',
    'S_MenuClose': '/Engine/VREditor/Sounds/UI/Floating_UI_Close',
}
LIB = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
report = {}
for name, source in SOUNDS.items():
    target = f'{DEST}/{name}'
    if LIB.does_asset_exist(target):
        LIB.delete_asset(target)
    # A commandlet's asset registry does not list engine content, so the source is loaded by path.
    original = unreal.load_asset(source)
    assert original, f'Missing engine sound {source}'
    asset = TOOLS.duplicate_asset(name, DEST, original)
    assert asset, f'Cannot copy {source}'
    assert LIB.save_asset(target, only_if_is_dirty=False), target
    try:
        duration = round(float(asset.get_editor_property('duration')), 3)
    except Exception:
        duration = None
    report[name] = dict(source=source, asset=target, type=asset.get_class().get_name(), seconds=duration)
    unreal.log(f'MENU_SOUND {name} <- {source} ({report[name]["type"]}, {duration} s)')
out = ROOT / 'evidence/04/menu_sounds.json'
out.write_text(json.dumps(report, indent=1, ensure_ascii=False), encoding='utf-8')
