"""Author DA_Gratia soft body (KawaiiPhysics) from the mesh skin and exported source colliders.

Run in the editor (commandlet) after building GratiaVREditorTools. Source colliders come
from export_source_cloth_cages.py (schema 2, Blender MCP). Chain physics values are
profile data and can be tuned in the Details panel afterwards.
"""
import json
from pathlib import Path
import unreal

root = Path(__file__).resolve().parents[2]
source = root / 'Exports/Gratia/GameRig/source_cloth_cages.json'
assert json.loads(source.read_text(encoding='utf-8'))['schema'] == 2, 'export schema 2 with colliders required'
lib = unreal.EditorAssetLibrary
mesh = lib.load_asset('/Game/Gratia/GameRig/SK_Gratia_Game')
profile = lib.load_asset('/Game/Characters/Profiles/DA_Gratia')
assert isinstance(mesh, unreal.SkeletalMesh) and isinstance(profile, unreal.GratiaCharacterProfile)
assert not list(mesh.get_editor_property('mesh_clothing_assets')), 'remove legacy Chaos cloth from the mesh first'
setup = unreal.GratiaSoftBodySetupLibrary

# One chain per side: mirrored bones can have opposite forward axes.
# Values: VRChat-PhysBone-like spring settings; butt is heavier/softer in the source.
presets = {
    'Breast': dict(bones=['DEF-breast_L', 'DEF-breast_R'], damping=0.12, stiffness=0.06, world_location=0.7,
                   world_rotation=0.7, collision=3.0, limit=25.0, gravity=0.2, grab=0.5, stretch=6.0),
    'Butt': dict(bones=['DEF-ass_L', 'DEF-ass_R'], damping=0.15, stiffness=0.04, world_location=0.75,
                 world_rotation=0.75, collision=2.5, limit=20.0, gravity=0.2, grab=0.4, stretch=4.0),
}
chains, soft_bones, report = [], [], []
for group, p in presets.items():
    for bone in p['bones']:
        measured = setup.measure_soft_bone(mesh, bone)  # out params only; None on failure
        assert measured, bone
        axis, length, radius, along = measured
        chain = unreal.GratiaSoftBodyChain()
        values = dict(name=f"{group}_{bone[-1]}", root_bones=[bone], forward_axis=axis, dummy_bone_length_cm=length,
                      damping=p['damping'], stiffness=p['stiffness'], world_damping_location=p['world_location'],
                      world_damping_rotation=p['world_rotation'], collision_radius_cm=p['collision'],
                      limit_angle_degrees=p['limit'], gravity_scale=p['gravity'], contact_radius_cm=radius,
                      contact_center_along_bone=along, allow_grab=True, grab_movement=p['grab'], max_grab_stretch_cm=p['stretch'])
        for key, value in values.items():
            chain.set_editor_property(key, value)
        chains.append(chain)
        soft_bones.append(bone)
        report.append(dict(chain=values['name'], bone=bone, axis=str(axis), length_cm=length, contact_radius_cm=radius, center_along=along))
colliders = setup.build_body_colliders(mesh, str(source), soft_bones, 25.0)
assert colliders, 'no body colliders near soft bones'
colliders = list(colliders)
settings = profile.get_editor_property('soft_body')
settings.set_editor_property('enabled', True)
settings.set_editor_property('chains', chains)
# Full trigger/grip lets the fingers sink this deep into a breast/butt (stronger squeeze).
settings.set_editor_property('squish_depth_cm', 3.5)
# Contact volume and surface dent = the palm itself (palm skin ~1.5 cm from the palm centre):
# nothing squeezes, dents or vibrates before the visible palm reaches the skin.
settings.set_editor_property('palm_radius_cm', 1.5)
settings.set_editor_property('press_palm_radius_cm', 1.5)
# The skin yields first (squash follows the press depth), then the whole part moves away.
settings.set_editor_property('hand_push_fraction', 0.4)
settings.set_editor_property('squash_amount', 0.55)
settings.set_editor_property('squash_response', 1.0)
settings.set_editor_property('body_colliders', colliders)
profile.set_editor_property('soft_body', settings)
# KawaiiPhysics owns these bones; the rigid secondary path must not simulate them too.
bones = list(profile.get_editor_property('secondary_bones'))
for bone in bones:
    if str(bone.get_editor_property('bone')) in soft_bones or bone.get_editor_property('group') == 3:
        bone.set_editor_property('safe_simulation', False)
profile.set_editor_property('secondary_bones', bones)
assert lib.save_loaded_asset(profile, only_if_is_dirty=False)
(root / 'evidence/04/soft_body_setup.json').write_text(json.dumps(dict(chains=report, colliders=len(colliders)), indent=2), encoding='utf-8')
unreal.log('GRATIA_SOFT_BODY_SETUP_PASS chains=%d colliders=%d' % (len(chains), len(colliders)))
