"""Author DA_Gratia spring chains (KawaiiPhysics) for hair, clothing decor, ears and tail.

Run in the editor (commandlet) after building GratiaVREditorTools. Chains come from the source
physics groups (Exports/Gratia/GameRig/physics_group_manifest.json, Blender MCP) plus the
epaulette fringe; their bones stop being Chaos secondary bodies (the rigid drives made hair and
decor look like stone). Values are profile data and can be tuned in the Details panel.
"""
import json
from pathlib import Path
import unreal

root = Path(__file__).resolve().parents[2]
manifest = json.loads((root / 'Exports/Gratia/GameRig/physics_group_manifest.json').read_text(encoding='utf-8'))
lib = unreal.EditorAssetLibrary
mesh = lib.load_asset('/Game/Gratia/GameRig/SK_Gratia_Game')
profile = lib.load_asset('/Game/Characters/Profiles/DA_Gratia')
assert isinstance(mesh, unreal.SkeletalMesh) and isinstance(profile, unreal.GratiaCharacterProfile)
setup = unreal.GratiaSoftBodySetupLibrary

# Group: 1 hair, 2 clothing/decor, 4 ears/tail. VRChat-PhysBone-like values: hair and decor lag
# behind body motion (world damping 0.5) and hang with gravity relative to the authored pose;
# ears are cartilage (stiff, little gravity).
presets = {
    'hair': dict(group=1, damping=0.12, stiffness=0.04, world=0.5, radius=1.0, gravity=1.0, grab=0.8, stretch=30.0),
    'cloth_tie': dict(group=2, damping=0.15, stiffness=0.06, world=0.5, radius=0.8, limit=45.0, gravity=1.0, grab=0.8, stretch=20.0),
    'cloth_boot_decor': dict(group=2, damping=0.12, stiffness=0.08, world=0.5, radius=0.5, limit=50.0, gravity=1.0, grab=0.8, stretch=8.0),
    'shoulder_pad': dict(group=2, damping=0.15, stiffness=0.1, world=0.5, radius=0.8, limit=40.0, gravity=1.0, grab=0.8, stretch=8.0),
    'ears': dict(group=4, damping=0.25, stiffness=0.3, world=0.7, radius=1.0, limit=20.0, gravity=0.15, grab=0.6, stretch=4.0),
    'tail_accessory': dict(group=4, damping=0.15, stiffness=0.08, world=0.5, radius=1.0, limit=45.0, gravity=0.8, grab=0.8, stretch=10.0),
}

definitions = []
for group in manifest['groups']:
    if group['kind'] not in presets:
        continue  # breasts/butt/thighs are the soft body (setup_soft_body.py)
    bones = {b['unreal_name']: b for b in group['bones']}
    roots = [name for name, b in bones.items() if b['unreal_parent'] not in bones]
    tips = [name for name in bones if not any(b['unreal_parent'] == name for b in bones.values())]
    # The end bones have no child in the game skeleton: the tip dummy is their source length.
    tip_cm = sum(bones[name]['rest_length_metres'] for name in tips) / len(tips) * 100.0
    definitions.append((group['chain_name'].replace('.', '_'), group['kind'], roots, tip_cm))
# Epaulette fringe: five single bones under each DEF-shoulder (Gratia_GameRig, 6.58-6.59 cm in
# Blender; not a source physics group).
for side in 'LR':
    definitions.append((f'DEF-shoulder_pad_{side}', 'shoulder_pad',
                        [f'DEF-shoulder_pad_{side}'] + [f'DEF-shoulder_pad_{side}_{i:03d}' for i in range(1, 5)], 6.59))

chains, spring_bones, report = [], set(), []
for name, kind, roots, tip_cm in definitions:
    p = presets[kind]
    measured = setup.measure_spring_chain(mesh, roots)  # out params only; None on failure
    assert measured, name
    axis, count = measured
    # Short strands (fringe, bangs) keep their shape; long ones swing freely.
    limit = p.get('limit', 35.0 if count - len(roots) <= 3 else 0.0)
    chain = unreal.GratiaSpringChain()
    values = dict(name=name, root_bones=roots, group=p['group'], forward_axis=axis, tip_length_cm=round(tip_cm, 2),
                  damping=p['damping'], stiffness=p['stiffness'], world_damping_location=p['world'],
                  world_damping_rotation=p['world'], collision_radius_cm=p['radius'], limit_angle_degrees=limit,
                  gravity_scale=p['gravity'], allow_grab=True, grab_movement=p['grab'], max_grab_stretch_cm=p['stretch'])
    for key, value in values.items():
        chain.set_editor_property(key, value)
    chains.append(chain)
    spring_bones.update(roots)
    report.append(dict(chain=name, kind=kind, group=p['group'], roots=roots, axis=str(axis), bones=count, tip_cm=round(tip_cm, 2), limit=limit))

profile.set_editor_property('spring_chains', chains)
profile.set_editor_property('spring_grab_radius_cm', 4.0)
# Bones below a spring root are KawaiiPhysics bones now; Chaos must not simulate them too.
parents = {}
for group in manifest['groups']:
    for b in group['bones']:
        parents[b['unreal_name']] = b['unreal_parent']
def in_spring_chain(bone):
    seen = set()
    while bone and bone not in seen:
        if bone in spring_bones:
            return True
        seen.add(bone)
        bone = parents.get(bone)
    return False
bones = list(profile.get_editor_property('secondary_bones'))
turned_off = []
for bone in bones:
    name = str(bone.get_editor_property('bone'))
    if in_spring_chain(name) and bone.get_editor_property('safe_simulation'):
        bone.set_editor_property('safe_simulation', False)
        turned_off.append(name)
profile.set_editor_property('secondary_bones', bones)
# Bool UFUNCTION with out params: None when validation fails (spring roots, Chaos overlap, ...).
assert profile.validate_profile() is not None, 'profile validation failed; run Test-CharacterProfiles.ps1 for the errors'
assert lib.save_loaded_asset(profile, only_if_is_dirty=False)
(root / 'evidence/05/spring_chain_setup.json').write_text(json.dumps(dict(chains=report, chaos_off=turned_off), indent=2), encoding='utf-8')
unreal.log('GRATIA_SPRING_SETUP_PASS chains=%d bones_off_chaos=%d' % (len(chains), len(turned_off)))
