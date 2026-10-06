"""Import actual free-play captures from a passed package flow check into scene cards."""
import hashlib
import json
from pathlib import Path
import unreal

ROOT = Path(__file__).resolve().parents[2]
BASE = '/Game/Gratia/Experience'
report = json.loads((ROOT / 'evidence/04/packaged_scene_flow_result.json').read_text(encoding='utf-8-sig'))
manifest = json.loads((ROOT / 'Builds/Windows/build_manifest.json').read_text(encoding='utf-8-sig'))
assert report['passed'] and report['build_id'] == manifest['build_id'], 'Only passed current-package captures can become previews'
library = unreal.load_asset(BASE + '/DA_SceneLibrary')
assert library
textures = {}
for name, screenshot in (('Atrium', 'scene_flow_Playing_00.png'), ('PulseStudio', 'scene_flow_Playing_01.png')):
    path = ROOT / 'evidence/04' / screenshot
    assert path.is_file(), path
    task = unreal.AssetImportTask()
    for key, value in dict(filename=str(path), destination_path=BASE + '/Previews', destination_name='T_' + name,
                           automated=True, replace_existing=True, save=True).items():
        task.set_editor_property(key, value)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.load_asset(BASE + '/Previews/T_' + name)
    assert isinstance(texture, unreal.Texture2D)
    texture.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    unreal.EditorAssetLibrary.set_metadata_tag(texture, 'GratiaCaptureBuild', manifest['build_id'])
    unreal.EditorAssetLibrary.set_metadata_tag(texture, 'GratiaCaptureSHA256', hashlib.sha256(path.read_bytes()).hexdigest())
    assert unreal.EditorAssetLibrary.save_loaded_asset(texture, only_if_is_dirty=False)
    textures[name] = texture
entries = list(library.get_editor_property('scenes'))
for entry in entries:
    id = str(entry.get_editor_property('id'))
    pulse = id == 'Pulse' or (id.startswith('Performance_') and int(id.split('_')[-1]) % 2 == 1)
    entry.set_editor_property('thumbnail', textures['PulseStudio' if pulse else 'Atrium'])
library.set_editor_property('scenes', entries)
assert unreal.EditorAssetLibrary.save_loaded_asset(library, only_if_is_dirty=False)
unreal.log('GRATIA_SCENE_PREVIEWS_READY capture_build=' + manifest['build_id'])
