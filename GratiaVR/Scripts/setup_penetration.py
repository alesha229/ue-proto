"""Author DA_Gratia penetration channels for the jointed primitive and the player's hands (AGratiaPenetrator).

Run in the editor (commandlet) like setup_soft_body.py. The channel geometry is derived at runtime
from the reference pose: entrance = centroid of the entrance bones, direction toward the pelvis
anchor (semantic Pelvis). Values are profile data; tune them in the Details panel afterwards
(Physics > Penetration). An adult character profile only.

Each channel also gets an opening morph on the character mesh (UGratiaExperienceToolsLibrary::
CreateChannelOpeningMorph): the skin around the entrance moves away from the axis and the stretch fades out
smoothly over a wide area, so a large shaft (a fist) opens the channel fully without tearing the few wall bones'
skin; the solver drives it by the opening (1 at MorphFullOpeningCm) and the bones only shape the lips.

How much the opening morphs stretch the skin is baked into the vertex colours (BakeMorphStretchToVertexColor; no
Gratia material uses them otherwise); the skin material
softens and tints the stretched skin by it times the morph weight the solver sends (GratiaStretchVaginal / Anal).
The skin and the suit's cut-out mask get compression that holds up at close range (BC7, an uncompressed mask).
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


def channel(name, entrance, depth, bones, capture=3.0, rest=0.4, falloff=2.5, morph='None', morph_full=4.0, bulges=(), resistance=(),
            stretch='None'):
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
                          stretch_parameter=stretch, bulges=swell, bulge_full_radius_cm=4.5, bulge_max_weight=1.0,
                          resistance=[unreal.Vector2D(d, t) for d, t in resistance]).items():
        value.set_editor_property(key, item)
    return value


def wire_stretched_skin():
    """Skin material: the stretch mask (vertex colour R, G) times the solver's GratiaStretch* weights blurs the skin
    texture (a coarser mip hides magnified texels and compression blocks), tints it toward a flushed pink and flattens
    the normal map's detail; textures get close-range compression. Idempotent (nodes tagged GratiaVR_Stretch*)."""
    mel = unreal.MaterialEditingLibrary
    skin = lib.load_asset('/Game/Gratia/CharacterMaterials/M_Gratia_Body_skin')
    body = lib.load_asset('/Game/Gratia/Textures/T_body_diff')
    textures = []
    for key, compression, srgb in (('body_diff', unreal.TextureCompressionSettings.TC_BC7, True),
                                   ('default_cloth2_diff', unreal.TextureCompressionSettings.TC_BC7, True),
                                   ('default_cloth2_alpha', unreal.TextureCompressionSettings.TC_GRAYSCALE, False)):
        texture = lib.load_asset('/Game/Gratia/Textures/T_' + key)
        texture.set_editor_property('compression_settings', compression)
        texture.set_editor_property('srgb', srgb)
        texture.set_editor_property('max_texture_size', 4096)
        assert lib.save_loaded_asset(texture, only_if_is_dirty=False), key
        textures.append(dict(texture=key, compression=str(compression), size=[texture.blueprint_get_size_x(), texture.blueprint_get_size_y()]))
    # The uncompressed mask is sampled as linear grayscale (a colour sampler fails the suit material's compile).
    suit = lib.load_asset('/Game/Gratia/CharacterMaterials/M_Gratia_Default_cloth_2')
    mask = lib.load_asset('/Game/Gratia/Textures/T_default_cloth2_alpha')
    for sample in mel.get_material_expressions(suit):
        if isinstance(sample, unreal.MaterialExpressionTextureSample) and sample.get_editor_property('texture') == mask:
            sample.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    errors = mel.recompile_material(suit)
    assert not errors, list(errors)
    assert lib.save_loaded_asset(suit, only_if_is_dirty=False)
    expressions = list(mel.get_material_expressions(skin))

    def tagged(tag):
        return next((e for e in expressions if str(e.get_editor_property('desc')) == 'GratiaVR_' + tag), None)

    def node(cls, tag, x, y):
        found = tagged(tag)
        if found:
            return found
        made = mel.create_material_expression(skin, cls, x, y)
        made.set_editor_property('desc', 'GratiaVR_' + tag)
        expressions.append(made)
        return made

    def link(a, out, b, pin):
        names = [str(n) for n in mel.get_material_expression_input_names(b)]
        resolved = pin if pin in names else 'None' if 'None' in names else names[0]
        assert mel.connect_material_expressions(a, out, b, resolved), (a.get_name(), out, b.get_name(), pin, names)

    toon, normal, to_world = tagged('ReadableToon'), tagged('Normal'), tagged('ToWorld')
    assert toon and normal and to_world, 'Skin toon graph (GratiaVR_ReadableToon/Normal/ToWorld) not found'
    color = next(e for e in expressions if isinstance(e, unreal.MaterialExpressionTextureSample)
                 and e.get_editor_property('texture') == body and str(e.get_editor_property('desc')) == '')
    colors = node(unreal.MaterialExpressionVertexColor, 'StretchMask', -1700, 700)
    masks = []
    for tag, pin, y, parameter in (('StretchV', 'R', 640, 'GratiaStretchVaginal'), ('StretchA', 'G', 820, 'GratiaStretchAnal')):
        weight = node(unreal.MaterialExpressionScalarParameter, tag + 'Weight', -1450, y + 80)
        weight.set_editor_property('parameter_name', parameter)
        weight.set_editor_property('default_value', 0.0)
        product = node(unreal.MaterialExpressionMultiply, tag + 'Product', -1200, y)
        link(colors, pin, product, 'A')
        link(weight, '', product, 'B')
        masks.append(product)
    total = node(unreal.MaterialExpressionAdd, 'StretchSum', -1000, 720)
    link(masks[0], '', total, 'A')
    link(masks[1], '', total, 'B')
    stretch = node(unreal.MaterialExpressionSaturate, 'Stretch', -820, 720)
    link(total, '', stretch, 'Input')
    blur = node(unreal.MaterialExpressionTextureSample, 'StretchBlur', -1000, 400)
    blur.set_editor_property('texture', body)
    blur.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    blur.set_editor_property('mip_value_mode', unreal.TextureMipValueMode.TMVM_MIP_BIAS)
    blur.set_editor_property('const_mip_value', 3)
    double = node(unreal.MaterialExpressionMultiply, 'StretchBlurAmount', -650, 560)
    double.set_editor_property('const_b', 2.0)
    link(stretch, '', double, 'A')
    blur_alpha = node(unreal.MaterialExpressionSaturate, 'StretchBlurAlpha', -500, 560)
    link(double, '', blur_alpha, 'Input')
    soft = node(unreal.MaterialExpressionLinearInterpolate, 'StretchSoft', -350, 300)
    link(color, 'RGB', soft, 'A')
    link(blur, 'RGB', soft, 'B')
    link(blur_alpha, '', soft, 'Alpha')
    white = node(unreal.MaterialExpressionConstant3Vector, 'StretchWhite', -650, 800)
    white.set_editor_property('constant', unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    pink = node(unreal.MaterialExpressionConstant3Vector, 'StretchPink', -650, 900)
    pink.set_editor_property('constant', unreal.LinearColor(1.04, 0.64, 0.66, 1.0))
    tint = node(unreal.MaterialExpressionLinearInterpolate, 'StretchTint', -350, 820)
    link(white, '', tint, 'A')
    link(pink, '', tint, 'B')
    link(stretch, '', tint, 'Alpha')
    tinted = node(unreal.MaterialExpressionMultiply, 'StretchTinted', -150, 300)
    link(soft, '', tinted, 'A')
    link(tint, '', tinted, 'B')
    link(tinted, '', toon, 'A')
    flat = node(unreal.MaterialExpressionConstant3Vector, 'StretchFlat', -1100, 150)
    flat.set_editor_property('constant', unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
    flatten = node(unreal.MaterialExpressionLinearInterpolate, 'StretchNormal', -800, 220)
    link(normal, 'RGB', flatten, 'A')
    link(flat, '', flatten, 'B')
    link(stretch, '', flatten, 'Alpha')
    link(flatten, '', to_world, 'Input')
    errors = mel.recompile_material(skin)
    assert not errors, list(errors)
    assert lib.save_loaded_asset(skin, only_if_is_dirty=False)
    return dict(material=skin.get_path_name(), mask='vertex colour R (vaginal), G (anal)', parameters=['GratiaStretchVaginal', 'GratiaStretchAnal'],
                textures=textures)


vag = [f'DEF-ero_vag_{side}{suffix}' for side in 'LR' for suffix in ('', '_001', '_002', '_003')]
anal = [f'DEF-ero_ass_{side}{suffix}' for side in 'LR' for suffix in ('', '_001')]
clit = ['DEF-ero_clit', 'DEF-ero_clit_001', 'DEF-ero_clit_002']
# Walls follow the shaft surface (each bone only as far as the surface reaches past its rest distance from the
# axis) and are dragged a little with its motion; the clit chain, further out, only yields to large sizes. The hip
# and buttock bones are an outer ring: they spread with the opening past 1.2-2 cm (clearly from a fist, XL and
# larger: 1.5-3 cm, up to 3-4 cm on the largest) and saturate gently.
# Opening morphs: 6.2 cm off the axis (the largest size, 4XL) within the core, fading over 12.5 cm (more than twice
# the opening, so no two vertices cross), from a little before the entrance through the channel's depth; the slit
# opens mostly across (40 % along it), the anus evenly. A fist drives them to about 0.6.
mesh = profile.get_editor_property('mesh')
pelvis = 'DEF-spine'
# Denser skin around the entrances so a wide opening bends smoothly instead of stretching a few large triangles:
# triangles within 9 cm of the entrance bones split into four, within 5 cm into sixteen (neighbours along split edges
# into two or three, no cracks). Each pass runs once per mesh (a marker attribute in the mesh description).
lib_tools = unreal.GratiaExperienceToolsLibrary
for marker, radius in (('GratiaChannelDetail_9cm', 9.0), ('GratiaChannelDetail_5cm', 5.0)):
    split = lib_tools.subdivide_mesh_around_bones(mesh, vag + anal + clit, radius, marker)
    assert split >= 0, f'Subdivision {marker} failed'
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
# Belly bulges along each channel: a deep, thick shaft pushes the belly in front of where it passes forward by up to
# 4.5 cm (a fist, XXL and larger drive them fully), the whole front however deep the path is, fading across the belly
# over 11 cm, never on the back, nor up to 5 cm above the vaginal entrance (the vulva, the
# pubic area and the thighs stay where they are; the swelling fades in over the next 5 cm).
BULGE_FLOOR_CM = 5.0
# Front = from the anus toward the vaginal entrance.
VAG_DEPTH, ANAL_DEPTH = 26.0, 36.0
bulges = {'Vaginal': [], 'Anal': []}
for name, entrance, depths, amount, radius in (('Vaginal', vag, (12.0, 17.0, 22.0), 4.5, 11.0),
                                               ('Anal', anal, (14.0, 20.0, 26.0, 32.0), 4.5, 11.0)):
    for index, depth in enumerate(depths, 1):
        morph = f'Gratia_Bulge{name}_{index}'
        moved = unreal.GratiaExperienceToolsLibrary.create_channel_bulge_morph(
            mesh, morph, entrance, pelvis, depth, vag, anal, radius, amount, BULGE_FLOOR_CM)
        assert moved > 0, f'Bulge morph {morph} moved no vertices'
        morphs[morph] = moved
        bulges[name].append((morph, depth))
# Stretched skin: log2 of the area ratio around every vertex with each opening morph at 1, over log2 6 (a sixfold
# area is full), vaginal in red and anal in green of the vertex colours.
stretched = lib_tools.bake_morph_stretch_to_vertex_color(mesh, ['Gratia_OpenVaginal', 'Gratia_OpenAnal'], 6.0)
assert stretched > 0, 'Stretch mask bake failed'
assert lib.save_loaded_asset(mesh, only_if_is_dirty=False)
# The profile guards the import by its morph count; the opening morphs are part of it now.
profile.set_editor_property('expected_morph_count', len(mesh.get_all_morph_target_names()))
channels = [
    channel('Vaginal', vag, VAG_DEPTH,
            [bone(b, 1.0, 0.0, 3.5, 0.012, 0.8) for b in vag]
            + [bone(b, 0.35, 0.5, 1.2, 0.006, 0.4) for b in clit]
            + [bone(b, 0.8, 1.5, 3.5, ring=True) for b in ('DEF-pelvis_L', 'DEF-pelvis_R')]
            + [bone(b, 0.7, 2.0, 3.0, ring=True) for b in ('DEF-ass_L', 'DEF-ass_R')], morph='Gratia_OpenVaginal', morph_full=OPENING_CM,
            bulges=bulges['Vaginal'], resistance=VAG_TIGHT, stretch='GratiaStretchVaginal'),
    channel('Anal', anal, ANAL_DEPTH,
            [bone(b, 1.0, 0.0, 3.5, 0.012, 0.8) for b in anal]
            + [bone(b, 1.0, 1.2, 4.0, ring=True) for b in ('DEF-ass_L', 'DEF-ass_R')]
            + [bone(b, 0.5, 2.0, 2.5, ring=True) for b in ('DEF-pelvis_L', 'DEF-pelvis_R')], rest=0.3,
            morph='Gratia_OpenAnal', morph_full=OPENING_CM, bulges=bulges['Anal'], resistance=ANAL_TIGHT, stretch='GratiaStretchAnal'),
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
report['stretched_vertex_instances'] = stretched
report['skin'] = wire_stretched_skin()
(root / 'evidence/06').mkdir(parents=True, exist_ok=True)
(root / 'evidence/06/penetration_setup.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
assert not errors, list(errors)
assert not any('Penetration' in str(w) for w in warnings), [str(w) for w in warnings if 'Penetration' in str(w)]
assert lib.save_loaded_asset(profile, only_if_is_dirty=False)
unreal.log('GRATIA_PENETRATION_SETUP_PASS channels=%d warnings=%d' % (len(channels), len(warnings)))
