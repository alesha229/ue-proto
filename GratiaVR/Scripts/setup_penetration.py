"""Author DA_Gratia penetration channels for the jointed primitive (AGratiaPenetrator).

Run in the editor (commandlet) like setup_soft_body.py. The channel geometry is derived at runtime
from the reference pose: entrance = centroid of the entrance bones, direction toward the pelvis
anchor (semantic Pelvis). Values are profile data; tune them in the Details panel afterwards
(Physics > Penetration). An adult character profile only.
"""
import json
from pathlib import Path
import unreal

root = Path(__file__).resolve().parents[2]
lib = unreal.EditorAssetLibrary
profile = lib.load_asset('/Game/Characters/Profiles/DA_Gratia')
assert isinstance(profile, unreal.GratiaCharacterProfile)
assert isinstance(profile.get_editor_property('mesh'), unreal.SkeletalMesh)


def bone(name, response=1.0, start=0.0, max_offset=3.0, drag=0.0, max_drag=0.8):
    value = unreal.GratiaChannelBone()
    for key, item in dict(bone=name, response=response, start_opening_cm=start, max_offset_cm=max_offset,
                          drag_seconds=drag, max_drag_cm=max_drag).items():
        value.set_editor_property(key, item)
    return value


def channel(name, entrance, depth, bones, capture=3.0, rest=0.4, falloff=2.5):
    value = unreal.GratiaPenetrationChannel()
    for key, item in dict(name=name, enabled=True, anchor_bone_semantic='Pelvis', entrance_bones=entrance,
                          inward_target_bone='None', depth_cm=depth, capture_radius_cm=capture,
                          capture_angle_degrees=50.0, release_angle_degrees=115.0, rest_radius_cm=rest,
                          wall_falloff_cm=falloff, bones=bones).items():
        value.set_editor_property(key, item)
    return value


vag = [f'DEF-ero_vag_{side}{suffix}' for side in 'LR' for suffix in ('', '_001', '_002', '_003')]
anal = [f'DEF-ero_ass_{side}{suffix}' for side in 'LR' for suffix in ('', '_001')]
clit = ['DEF-ero_clit', 'DEF-ero_clit_001', 'DEF-ero_clit_002']
# Walls follow the shaft surface and are dragged a little with its motion; the clit chain and the
# outer ring (hips, buttocks) only yield to large sizes and saturate smoothly.
channels = [
    channel('Vaginal', vag, 14.0,
            [bone(b, 1.0, 0.0, 3.5, 0.012, 0.8) for b in vag]
            + [bone(b, 0.35, 0.5, 1.2, 0.006, 0.4) for b in clit]
            + [bone(b, 0.3, 2.0, 2.0) for b in ('DEF-pelvis_L', 'DEF-pelvis_R')]
            + [bone(b, 0.2, 2.5, 1.5) for b in ('DEF-ass_L', 'DEF-ass_R')]),
    channel('Anal', anal, 18.0,
            [bone(b, 1.0, 0.0, 3.5, 0.012, 0.8) for b in anal]
            + [bone(b, 0.45, 1.5, 3.0) for b in ('DEF-ass_L', 'DEF-ass_R')]
            + [bone(b, 0.2, 2.5, 1.5) for b in ('DEF-pelvis_L', 'DEF-pelvis_R')], rest=0.3),
]
settings = profile.get_editor_property('penetration')
settings.set_editor_property('enabled', True)
settings.set_editor_property('channels', channels)
profile.set_editor_property('penetration', settings)
# Missing bones are only warnings for other models (the channel is disabled); for Gratia every
# bone must exist, so a penetration warning fails the setup before anything is saved.
# Bool UFUNCTIONs with out parameters return the outputs (None when the validation failed).
result = profile.validate_profile()
errors, warnings = result if result is not None else (['ValidateProfile failed; see the log'], [])
report = dict(channels=[dict(name=str(c.get_editor_property('name')), depth_cm=c.get_editor_property('depth_cm'),
                             entrance=[str(b) for b in c.get_editor_property('entrance_bones')],
                             bones=[str(b.get_editor_property('bone')) for b in c.get_editor_property('bones')]) for c in channels],
              errors=[str(e) for e in errors], warnings=[str(w) for w in warnings])
(root / 'evidence/06').mkdir(parents=True, exist_ok=True)
(root / 'evidence/06/penetration_setup.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
assert not errors, list(errors)
assert not any('Penetration' in str(w) for w in warnings), [str(w) for w in warnings if 'Penetration' in str(w)]
assert lib.save_loaded_asset(profile, only_if_is_dirty=False)
unreal.log('GRATIA_PENETRATION_SETUP_PASS channels=%d warnings=%d' % (len(channels), len(warnings)))
