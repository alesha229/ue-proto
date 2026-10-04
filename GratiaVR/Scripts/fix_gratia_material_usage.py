"""Persist both material usages required by the animated morph-capable model."""
import json
from pathlib import Path
import unreal

ROOT = Path(r'E:\coding\ue proto')
library = unreal.EditorAssetLibrary
names = sorted(file.stem for file in (ROOT / 'GratiaVR/Content/Gratia/CharacterMaterials').glob('M_Gratia_*.uasset'))
assert len(names) == 11
report = []
for name in names:
    material = library.load_asset('/Game/Gratia/CharacterMaterials/' + name)
    before = {key: material.get_editor_property(key) for key in ('used_with_skeletal_mesh', 'used_with_morph_targets')}
    material.set_editor_property('used_with_skeletal_mesh', True)
    material.set_editor_property('used_with_morph_targets', True)
    errors = unreal.MaterialEditingLibrary.recompile_material(material)
    assert not errors, (name, errors)
    assert library.save_loaded_asset(material)
    report.append({'material': material.get_path_name(), 'before': before,
        'after': {key: material.get_editor_property(key) for key in before}})
(ROOT / 'evidence/02/animated_material_usage_fix.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('GRATIA_ANIMATED_MATERIAL_USAGE_SAVED')
