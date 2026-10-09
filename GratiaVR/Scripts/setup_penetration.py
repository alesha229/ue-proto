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


def bone(name, response=1.0, start=0.0, max_offset=3.0, drag=0.0, max_drag=0.8, ring=False):
    value = unreal.GratiaChannelBone()
    for key, item in dict(bone=name, response=response, start_opening_cm=start, max_offset_cm=max_offset,
                          drag_seconds=drag, max_drag_cm=max_drag, outer_ring=ring).items():
        value.set_editor_property(key, item)
    return value


def channel(name, entrance, depth, bones, capture=3.0, rest=0.4, falloff=2.5, morph='None', morph_full=4.0, bulges=(), resistance=()):
    value = unreal.GratiaPenetrationChannel()
    swell = []
    for bulge_morph, bulge_depth in bulges:
        item = unreal.GratiaChannelBulge()
        item.set_editor_property('morph', bulge_morph)
        item.set_editor_property('depth_cm', bulge_depth)
        swell.append(item)
    for key, item in dict(name=name, enabled=True, anchor_bone_semantic='Pelvis', entrance_bones=entrance,
                          inward_target_bone='None', depth_cm=depth, capture_radius_cm=capture,
                          capture_angle_degrees=50.0, release_angle_degrees=115.0, rest_radius_cm=rest,
                          wall_falloff_cm=falloff, bones=bones, opening_morph=morph, morph_full_opening_cm=morph_full,
                          bulges=swell, bulge_full_radius_cm=4.5,
                          resistance=[unreal.Vector2D(d, t) for d, t in resistance]).items():
        value.set_editor_property(key, item)
    return value


vag = [f'DEF-ero_vag_{side}{suffix}' for side in 'LR' for suffix in ('', '_001', '_002', '_003')]
anal = [f'DEF-ero_ass_{side}{suffix}' for side in 'LR' for suffix in ('', '_001')]
clit = ['DEF-ero_clit', 'DEF-ero_clit_001', 'DEF-ero_clit_002']
# Walls follow the shaft surface (each bone only as far as the surface reaches past its rest distance from the
# axis) and are dragged a little with its motion; the clit chain, further out, only yields to large sizes. The hip
# and buttock bones are an outer ring: they spread with the opening only past 2.5-3.5 cm (a fist, 3XL, 4XL) and
# saturate gently, so they never stretch the thigh for ordinary sizes.
# Opening morphs: 6.2 cm off the axis (the largest size, 4XL) within the core, fading over 12.5 cm (more than twice
# the opening, so no two vertices cross), from a little before the entrance through the channel's depth; the slit
# opens mostly across (40 % along it), the anus evenly. A fist drives them to about 0.6.
mesh = profile.get_editor_property('mesh')
pelvis = 'DEF-spine'
OPENING_CM = 6.2
morphs = {}
for morph, entrance, depth, left, right, core, outside, along in (
        ('Gratia_OpenVaginal', vag, 8.0, vag[:4], vag[4:], 0.8, 2.5, 0.4),
        ('Gratia_OpenAnal', anal, 8.0, [], [], 0.6, 2.0, 1.0)):
    moved = unreal.GratiaExperienceToolsLibrary.create_channel_opening_morph(
        mesh, morph, entrance, pelvis, left, right, OPENING_CM, core, 12.5, outside, depth, along)
    assert moved > 0, f'Opening morph {morph} moved no vertices'
    morphs[morph] = moved
# Resistance (depth cm, tightness 0..1): a tight entrance ring, an easy middle, tight again deep - the cervix at
# 15-18 cm, the second sphincter at 15-20 cm - and moderate beyond. A thicker shaft feels it more, a thinner less.
VAG_TIGHT = [(0.0, 0.75), (2.0, 0.55), (4.0, 0.25), (12.0, 0.3), (15.0, 0.7), (18.0, 0.85), (26.0, 0.6)]
ANAL_TIGHT = [(0.0, 0.95), (2.5, 0.8), (4.0, 0.35), (14.0, 0.4), (17.0, 0.75), (20.0, 0.45), (36.0, 0.5)]
# Belly bulges along each channel: a deep, thick shaft swells the body in front of where it passes (2.2-2.5 cm at
# full, fading over 11-12 cm, never on the back). Front = from the anus toward the vaginal entrance.
VAG_DEPTH, ANAL_DEPTH = 26.0, 36.0
bulges = {'Vaginal': [], 'Anal': []}
for name, entrance, depths, amount, radius in (('Vaginal', vag, (12.0, 17.0, 22.0), 2.2, 11.0),
                                               ('Anal', anal, (14.0, 20.0, 26.0, 32.0), 2.5, 12.0)):
    for index, depth in enumerate(depths, 1):
        morph = f'Gratia_Bulge{name}_{index}'
        moved = unreal.GratiaExperienceToolsLibrary.create_channel_bulge_morph(
            mesh, morph, entrance, pelvis, depth, vag, anal, radius, amount)
        assert moved > 0, f'Bulge morph {morph} moved no vertices'
        morphs[morph] = moved
        bulges[name].append((morph, depth))
assert lib.save_loaded_asset(mesh, only_if_is_dirty=False)
# The profile guards the import by its morph count; the opening morphs are part of it now.
profile.set_editor_property('expected_morph_count', len(mesh.get_all_morph_target_names()))
channels = [
    channel('Vaginal', vag, VAG_DEPTH,
            [bone(b, 1.0, 0.0, 3.5, 0.012, 0.8) for b in vag]
            + [bone(b, 0.35, 0.5, 1.2, 0.006, 0.4) for b in clit]
            + [bone(b, 0.4, 3.0, 2.0, ring=True) for b in ('DEF-pelvis_L', 'DEF-pelvis_R')]
            + [bone(b, 0.3, 3.5, 1.5, ring=True) for b in ('DEF-ass_L', 'DEF-ass_R')], morph='Gratia_OpenVaginal', morph_full=OPENING_CM,
            bulges=bulges['Vaginal'], resistance=VAG_TIGHT),
    channel('Anal', anal, ANAL_DEPTH,
            [bone(b, 1.0, 0.0, 3.5, 0.012, 0.8) for b in anal]
            + [bone(b, 0.55, 2.5, 3.0, ring=True) for b in ('DEF-ass_L', 'DEF-ass_R')]
            + [bone(b, 0.25, 3.5, 1.2, ring=True) for b in ('DEF-pelvis_L', 'DEF-pelvis_R')], rest=0.3,
            morph='Gratia_OpenAnal', morph_full=OPENING_CM, bulges=bulges['Anal'], resistance=ANAL_TIGHT),
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
