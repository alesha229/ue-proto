"""Restore readable texture-based toon materials; run in Unreal Editor Python.

The Blender *_metal images classify toon shading with RGB colors. They are not
PBR metallic maps: feeding their red channel into Metallic made white fabric
reflect a mostly empty environment and turn black. Keep authored albedo colors,
including dark costume panels, instead of guessing new material classifications.

This script changes only character materials and their sampled color/mask texture
settings. It does not import geometry, change morphs, replace actors, alter room
lights, or start play. The caller owns editor/build lifecycle and visual checks.
"""
import datetime
import hashlib
import json
import re
import shutil
from pathlib import Path

import unreal


ROOT = Path(r'E:\coding\ue proto')
OUT = ROOT / 'evidence/02'
stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
BACKUP = OUT / ('appearance_before_' + stamp)
BACKUP.mkdir(parents=True, exist_ok=False)
library = unreal.EditorAssetLibrary
edit = unreal.MaterialEditingLibrary
content = Path(unreal.Paths.project_content_dir()).resolve()

# Two-sided costume/ear/hair surfaces cover the exported base mesh, whose Blender
# Solidify modifier was deliberately omitted to preserve all shape keys.
# Alpha conventions match the existing preview, including inverted cloth2 R.
SPECS = {
    'Body_skin': ('body_diff', None, False),
    'Nails': ('nails', None, False),
    'Default cloth 1': ('default_cloth1_diff', None, True),
    'Default cloth 2': ('default_cloth2_diff', 'inverse_mask', True),
    'Gratia ears': ('ears_diff', None, True),
    'Gratia_face': ('face_diff', None, False),
    'Gratia_brows': ('face_diff', 'diffuse_alpha', True),
    'Gratia_eyes': ('eyes_diff', None, False),
    'Gratia_eyeshadows': ('eyeshadows', 'diffuse_alpha', True),
    'Gratia_eye_detail': ('eyes_detail', 'diffuse_alpha', True),
    'Gratia_hair': ('hair_diff', None, True),
}


def material_path(label):
    return '/Game/Gratia/CharacterMaterials/M_Gratia_' + re.sub('[^A-Za-z0-9_]', '_', label)


def texture_path(key):
    return '/Game/Gratia/Textures/T_' + key


mesh = library.load_asset('/Game/Gratia/Character/SK_Gratia')
assert isinstance(mesh, unreal.SkeletalMesh), 'Gratia mesh must exist before appearance repair'
original_morphs = tuple(m.get_name() for m in mesh.get_editor_property('morph_targets'))
original_slots = tuple((str(s.get_editor_property('imported_material_slot_name')),
                        s.get_editor_property('material_interface').get_path_name()
                        if s.get_editor_property('material_interface') else None)
                       for s in mesh.get_editor_property('materials'))
assert len(original_morphs) == 50 and 'Mouth O wide' in original_morphs
materials = {label: library.load_asset(material_path(label)) for label in SPECS}
assert all(isinstance(m, unreal.Material) for m in materials.values()), 'Expected all 11 existing materials'
color_keys = sorted({spec[0] for spec in SPECS.values()})
textures = {key: library.load_asset(texture_path(key)) for key in color_keys + ['default_cloth2_alpha']}
assert all(isinstance(t, unreal.Texture2D) for t in textures.values()), 'Expected source diffuse and mask textures'

# Save the current graph first, then copy its packages outside Content so backups
# do not get cooked. Restore with the editor closed if a visual comparison fails.
backups = []
for asset in list(materials.values()) + list(textures.values()):
    assert library.save_loaded_asset(asset, only_if_is_dirty=False), asset.get_path_name()
    package = asset.get_path_name().split('.')[0]
    assert package.startswith('/Game/'), package
    relative = Path(package.removeprefix('/Game/'))
    source_base = content / relative
    assert source_base.with_suffix('.uasset').exists(), str(source_base)
    for suffix in ('.uasset', '.uexp', '.ubulk'):
        source = source_base.with_suffix(suffix)
        if source.exists():
            target = BACKUP / 'Content' / relative.with_suffix(suffix)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            backups.append({'asset': package, 'file': str(target),
                            'sha256': hashlib.sha256(target.read_bytes()).hexdigest()})
for name in ('packaged_gratia_preview.png', 'packaged_pose_Arms.png', 'packaged_pose_Head.png',
             'appearance_fix_manifest.json'):
    previous = OUT / name
    if previous.exists():
        shutil.copy2(previous, BACKUP / name)
shutil.copy2(ROOT / 'GratiaVR/Scripts/fix_gratia_vr_appearance.py', BACKUP / 'fix_gratia_vr_appearance.py')
(BACKUP / 'backup_manifest.json').write_text(json.dumps(backups, indent=2), encoding='utf-8')

texture_report = []
for key, texture in textures.items():
    is_mask = key == 'default_cloth2_alpha'
    before = {name: str(texture.get_editor_property(name))
              for name in ('srgb', 'compression_settings', 'compression_no_alpha', 'max_texture_size')}
    texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_DEFAULT)
    texture.set_editor_property('srgb', not is_mask)
    texture.set_editor_property('compression_no_alpha', False)
    # Existing 2K caps are preserved. Source PNG files and atlas colors are unchanged.
    assert library.save_loaded_asset(texture, only_if_is_dirty=False)
    texture_report.append({'texture': texture.get_path_name(), 'before': before,
                           'after': {'srgb': not is_mask, 'compression': 'TC_DEFAULT', 'alpha_preserved': True}})

material_report = []
for label, (diffuse_key, mask, two_sided) in SPECS.items():
    material = materials[label]
    before = {name: str(material.get_editor_property(name))
              for name in ('shading_model', 'blend_mode', 'two_sided', 'used_with_skeletal_mesh', 'used_with_morph_targets')}
    clip_value = material.get_editor_property('opacity_mask_clip_value') if mask else None
    if mask:
        assert material.get_editor_property('blend_mode') == unreal.BlendMode.BLEND_MASKED, label
    # UE 5.8 DeleteAllMaterialExpressions removes from the array it iterates.
    # Keep the existing graph and reconnect its diffuse sample instead.
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED if mask else unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property('two_sided', two_sided)
    material.set_editor_property('used_with_skeletal_mesh', True)
    material.set_editor_property('used_with_morph_targets', True)

    samples = [n for n in edit.get_material_expressions(material)
               if isinstance(n, unreal.MaterialExpressionTextureSample)
               and n.get_editor_property('texture') == textures[diffuse_key]]
    color = samples[0] if samples else edit.create_material_expression(material, unreal.MaterialExpressionTextureSample, -400, 0)
    color.set_editor_property('texture', textures[diffuse_key])
    color.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    color.set_editor_property('const_coordinate', 0)
    assert edit.connect_material_property(color, 'RGB', unreal.MaterialProperty.MP_EMISSIVE_COLOR), label
    if mask == 'diffuse_alpha':
        assert edit.connect_material_property(color, 'A', unreal.MaterialProperty.MP_OPACITY_MASK), label
    elif mask == 'inverse_mask':
        alpha = edit.create_material_expression(material, unreal.MaterialExpressionTextureSample, -400, 250)
        alpha.set_editor_property('texture', textures['default_cloth2_alpha'])
        alpha.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        alpha.set_editor_property('const_coordinate', 0)
        inverse = edit.create_material_expression(material, unreal.MaterialExpressionOneMinus, -100, 250)
        assert edit.connect_material_expressions(alpha, 'R', inverse, 'None'), label
        assert edit.connect_material_property(inverse, '', unreal.MaterialProperty.MP_OPACITY_MASK), label
    if mask:
        material.set_editor_property('opacity_mask_clip_value', clip_value)

    # UE 5.8 returns compile diagnostics; do not save a graph that silently falls back.
    errors = edit.recompile_material(material)
    assert not errors, {'material': material.get_path_name(), 'compiler_errors': list(errors)}
    assert edit.get_material_property_input_node(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR) == color
    # Unlit shading does not evaluate the old normal/metallic branches.
    assert material.get_editor_property('used_with_skeletal_mesh')
    assert material.get_editor_property('used_with_morph_targets')
    assert library.save_loaded_asset(material, only_if_is_dirty=False), label
    material_report.append({'material': material.get_path_name(), 'before': before,
                            'diffuse': textures[diffuse_key].get_path_name(), 'output': 'RGB -> Emissive (gain 1)',
                            'shading': 'MSM_UNLIT', 'mask': mask, 'mask_clip': clip_value, 'two_sided': two_sided,
                            'skeletal_usage': True, 'morph_usage': True, 'compile_errors': []})

assert original_morphs == tuple(m.get_name() for m in mesh.get_editor_property('morph_targets'))
assert original_slots == tuple((str(s.get_editor_property('imported_material_slot_name')),
                                s.get_editor_property('material_interface').get_path_name()
                                if s.get_editor_property('material_interface') else None)
                               for s in mesh.get_editor_property('materials'))
report = {
    'applied_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'backup_directory': str(BACKUP), 'backup_manifest': str(BACKUP / 'backup_manifest.json'),
    'design': 'Texture-based unlit toon preview; preserve authored RGB/alpha; remove incorrect PBR mask interpretation',
    'root_cause': '*_metal RGB maps were connected as red-channel PBR Metallic; empty reflections blackened white costume fabric',
    'room_lighting': 'Unchanged; character colors no longer require room reflections or direct light',
    'textures': texture_report, 'materials': material_report, 'morph_count': len(original_morphs),
    'visual_validation': 'Pending packaged front/side/back and close face screenshots; check masks, blink and dark authored costume panels in VR',
}
(OUT / 'appearance_fix_manifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('GRATIA_VR_APPEARANCE_FIXED')
