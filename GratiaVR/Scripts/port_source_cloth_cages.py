"""Active post-MCP import of original cloth cages, masks and pin weights."""
import json
import shutil
from pathlib import Path
import unreal

root = Path(__file__).resolve().parents[2]
source = root / 'Exports/Gratia/GameRig/source_cloth_cages.json'
data = json.loads(source.read_text(encoding='utf-8'))
assert data['schema'] == 1 and all(c['weights'] for c in data['cages'])
lib = unreal.EditorAssetLibrary
mesh = lib.load_asset('/Game/Gratia/GameRig/SK_Gratia_Game')
profile = lib.load_asset('/Game/Characters/Profiles/DA_Gratia')
assert isinstance(mesh, unreal.SkeletalMesh) and isinstance(profile, unreal.GratiaCharacterProfile)
backup = root / 'evidence/04/before_source_cloth'
backup.mkdir(parents=True, exist_ok=True)
for asset in (mesh, profile):
    package = asset.get_path_name().split('.')[0].removeprefix('/Game/')
    asset_file = root / 'GratiaVR/Content' / (package + '.uasset')
    if not (backup / asset_file.name).exists():
        shutil.copy2(asset_file, backup / asset_file.name)
# Semantic roots come from the profile: their skinned vertices (head, hair, feet)
# have no SurfaceDeform in the source and must stay out of shared-material masks.
excluded = [profile.resolve_bone(s) for s in ('Head', 'LeftFoot', 'RightFoot')]
assert all(str(b) != 'None' for b in excluded), excluded
assert unreal.GratiaClothPortLibrary.build_source_cloth_cages(mesh, str(source), excluded)
# Persist the cloth vertex-factory permutation before cook; editor auto-usage
# alone cannot guarantee that the standalone package contains these shaders.
for slot in mesh.get_editor_property('materials'):
    material = slot.get_editor_property('material_interface')
    if isinstance(material, unreal.Material) and not material.get_editor_property('used_with_clothing'):
        material.set_editor_property('used_with_clothing', True)
        assert not unreal.MaterialEditingLibrary.recompile_material(material)
        assert lib.save_loaded_asset(material)
name = 'GratiaSourceCloth_BodyCages'
enabled = [c for c in data['cages'] if c['enabled_in_source_render']]
count = sum(len(c['vertices']) for c in enabled)
definition = unreal.GratiaSourceClothCage()
for key, value in dict(asset_name=name, group=3, expected_particle_count=count).items():
    definition.set_editor_property(key, value)
profile.set_editor_property('source_cloth_cages', [definition])
regions = []
first = 0
semantics = {'TitsPhys':'UpperChest', 'AssPhys':'Pelvis', 'ThighsPhys':'LeftThigh'}
for cage in enabled:
    region = unreal.GratiaSourceClothRegion()
    for key, value in dict(name=cage['name'], asset_name=name, first_particle=first,
                           particle_count=len(cage['vertices']), anchor_semantic=semantics[cage['name']]).items():
        region.set_editor_property(key, value)
    regions.append(region)
    first += len(cage['vertices'])
profile.set_editor_property('source_cloth_regions', regions)
settings = profile.get_editor_property('cloth_settings')
settings.set_editor_property('enabled', True)
profile.set_editor_property('cloth_settings', settings)
# Soft-body surface is now driven by cloth; do not simulate it twice through DEF bones.
bones = list(profile.get_editor_property('secondary_bones'))
for bone in bones:
    if bone.get_editor_property('group') == 3:
        bone.set_editor_property('safe_simulation', False)
profile.set_editor_property('secondary_bones', bones)
errors = []
warnings = []
clothing = list(mesh.get_editor_property('mesh_clothing_assets'))
assert clothing and all(c is not None and c.get_name() for c in clothing), clothing
assert any(c.get_name() == name and c.get_class().get_name() == 'GratiaSourceClothingAsset' for c in clothing), clothing
assert lib.save_loaded_asset(mesh, only_if_is_dirty=False)
assert lib.save_loaded_asset(profile, only_if_is_dirty=False)
report = dict(mesh=mesh.get_path_name(), asset=name, particles=count,
              regions=[dict(name=c['name'], vertices=len(c['vertices'])) for c in enabled],
              source=data['source'], approximation='Native Chaos cloth solver; original cage topology, pin weights and SurfaceDeform masks',
              limits='Native active cloth vertices blend away positional corrective morphs; face and unrelated vertices remain skinned')
(root / 'evidence/04/source_cloth_port.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('GRATIA_SOURCE_CLOTH_PORT_PASS')
