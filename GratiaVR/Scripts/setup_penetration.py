"""Author DA_Gratia penetration channels for the jointed primitive and the player's hands (AGratiaPenetrator).

Run in the editor (commandlet) like setup_soft_body.py. The channel geometry is derived at runtime
from the reference pose: entrance = centroid of the entrance bones, direction toward the pelvis
anchor (semantic Pelvis). Values are profile data; tune them in the Details panel afterwards
(Physics > Penetration). An adult character profile only.

Each channel also gets an opening morph on the character mesh (UGratiaExperienceToolsLibrary::
CreateChannelOpeningMorph): the skin around the entrance moves away from the axis and the stretch fades out
smoothly over a wide area, so a large shaft (a fist) opens the channel fully without tearing the few wall bones'
skin; the solver drives it by the opening (1 at MorphFullOpeningCm) and the bones only shape the lips.
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


def channel(name, entrance, depth, bones, capture=3.0, rest=0.4, falloff=2.5, morph='None', morph_full=4.0):
    value = unreal.GratiaPenetrationChannel()
    for key, item in dict(name=name, enabled=True, anchor_bone_semantic='Pelvis', entrance_bones=entrance,
                          inward_target_bone='None', depth_cm=depth, capture_radius_cm=capture,
                          capture_angle_degrees=50.0, release_angle_degrees=115.0, rest_radius_cm=rest,
                          wall_falloff_cm=falloff, bones=bones, opening_morph=morph, morph_full_opening_cm=morph_full).items():
        value.set_editor_property(key, item)
    return value


vag = [f'DEF-ero_vag_{side}{suffix}' for side in 'LR' for suffix in ('', '_001', '_002', '_003')]
anal = [f'DEF-ero_ass_{side}{suffix}' for side in 'LR' for suffix in ('', '_001')]
clit = ['DEF-ero_clit', 'DEF-ero_clit_001', 'DEF-ero_clit_002']
# Walls follow the shaft surface (each bone only as far as the surface reaches past its rest distance from the
# axis) and are dragged a little with its motion; the clit chain, further out, only yields to large sizes. The big
# hip and buttock bones are not part of a channel: moving them stretched half the thigh.
# Opening morphs: 4 cm off the axis (a fist's radius) within the core, fading over 8.5 cm (more than twice the
# opening, so no two vertices cross), from a little before the entrance through the channel's depth; the slit opens
# mostly across (40 % along it), the anus evenly.
mesh = profile.get_editor_property('mesh')
pelvis = 'DEF-spine'
OPENING_CM = 4.0
morphs = {}
for morph, entrance, depth, left, right, core, outside, along in (
        ('Gratia_OpenVaginal', vag, 14.0, vag[:4], vag[4:], 0.8, 2.5, 0.4),
        ('Gratia_OpenAnal', anal, 18.0, [], [], 0.6, 2.0, 1.0)):
    moved = unreal.GratiaExperienceToolsLibrary.create_channel_opening_morph(
        mesh, morph, entrance, pelvis, left, right, OPENING_CM, core, 8.5, outside, depth, along)
    assert moved > 0, f'Opening morph {morph} moved no vertices'
    morphs[morph] = moved
assert lib.save_loaded_asset(mesh, only_if_is_dirty=False)
# The profile guards the import by its morph count; the opening morphs are part of it now.
profile.set_editor_property('expected_morph_count', len(mesh.get_all_morph_target_names()))
channels = [
    channel('Vaginal', vag, 14.0,
            [bone(b, 1.0, 0.0, 3.5, 0.012, 0.8) for b in vag]
            + [bone(b, 0.35, 0.5, 1.2, 0.006, 0.4) for b in clit], morph='Gratia_OpenVaginal', morph_full=OPENING_CM),
    channel('Anal', anal, 18.0,
            [bone(b, 1.0, 0.0, 3.5, 0.012, 0.8) for b in anal], rest=0.3, morph='Gratia_OpenAnal', morph_full=OPENING_CM),
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
              morphs=morphs, errors=[str(e) for e in errors], warnings=[str(w) for w in warnings])
(root / 'evidence/06').mkdir(parents=True, exist_ok=True)
(root / 'evidence/06/penetration_setup.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
assert not errors, list(errors)
assert not any('Penetration' in str(w) for w in warnings), [str(w) for w in warnings if 'Penetration' in str(w)]
assert lib.save_loaded_asset(profile, only_if_is_dirty=False)
unreal.log('GRATIA_PENETRATION_SETUP_PASS channels=%d warnings=%d' % (len(channels), len(warnings)))
