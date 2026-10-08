"""Author the player experience (ViRo Playspace look); run after the editor build.

Builds, in /Game/Gratia/Experience:
  * materials (HLSL custom nodes): synthwave sky with sun or moon and stars, scrolling neon grid,
    neon tubes/orbs/lasers and an LED floor that pulse with the music (MPC_Music), velvet with
    folds, wood, lacquer and marble (lit, with a little self-light: forward VR has no GI), and
    the menu's UI materials (glass panel, hexagon picture frame, gradient pill buttons);
  * the Nunito font (SIL OFL, Exports/Gratia/Fonts/Nunito);
  * music: generated loops (generate_scene_music.py) and the player's own tracks
    (prepare_user_music.py, git-ignored, personal use) with their light/mix analysis and
    left/right channels for the environment speakers; speaker attenuation and room reverbs;
  * the scene entries of the Fab environments (setup_fab_environments.py: Wabi Sabi guesthouse,
    Soul: City night quarter) with their backdrop levels;
  * the scene library (scenes, playlist, lobby music, look assets).
Only development tooling uses Python; the game loads cooked assets. The script re-authors only
when it or its inputs change (metadata on the library).
"""
import array
import hashlib
import json
import math
import wave
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'evidence/experience'
OUT.mkdir(parents=True, exist_ok=True)
BASE = '/Game/Gratia/Experience'
USER_BASE = '/Game/Gratia/UserMusic'
LIB = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LEVELS = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EDIT = unreal.MaterialEditingLibrary
GENERATED = ROOT / 'Exports/Gratia/Audio/Generated'
USER = ROOT / 'Exports/Gratia/Audio/UserMusic'
FONTS = ROOT / 'Exports/Gratia/Fonts/Nunito'

AUTHORING_HASH = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
input_hash = hashlib.sha256()
for source in sorted([ROOT / 'GratiaVR/Content/Characters/Profiles/DA_Gratia.uasset', ROOT / 'Exports/Gratia/Audio/KM466_Music.wav',
                      OUT / 'fab_environments.json', *GENERATED.glob('*.analysis.json'), *USER.glob('*.analysis.json'), *FONTS.glob('*.ttf')]):
    input_hash.update(str(source.relative_to(ROOT)).encode())
    if source.is_file():
        input_hash.update(hashlib.sha256(source.read_bytes()).digest())
INPUT_HASH = input_hash.hexdigest()
existing = unreal.load_asset(BASE + '/DA_SceneLibrary') if LIB.does_asset_exist(BASE + '/DA_SceneLibrary') else None
SHOULD_AUTHOR = not (existing and LIB.get_metadata_tag(existing, 'GratiaExperienceAuthoring') == AUTHORING_HASH
                     and LIB.get_metadata_tag(existing, 'GratiaExperienceInputs') == INPUT_HASH)

# ------------------------------------------------------------------------------------------------
# HLSL of the materials. Inputs are named expression nodes (see material()).

SKY_HLSL = r"""
float3 d = -normalize(CamVec);
float h = d.z;
float up = saturate(h);
float3 col = lerp(Horizon.rgb, Zenith.rgb, pow(up, 0.45));
col += Accent.rgb * (0.5 + 0.7 * Bass) * exp(-abs(h) * 9.0);
col = lerp(col, Ground.rgb, saturate(-h * 5.0));
float az = atan2(d.y, d.x);
float el = asin(clamp(h, -1.0, 1.0));
float2 g = float2(az, el) * Stars;
float2 c = floor(g);
float2 f = frac(g) - 0.5;
float r = frac(sin(dot(c, float2(12.9898, 78.233))) * 43758.5453);
float r2 = frac(sin(dot(c, float2(39.346, 11.135))) * 24634.6345);
float star = step(0.80, r) * smoothstep(0.11, 0.0, length(f + (float2(r2, r) - 0.5) * 0.5)) * saturate(h * 6.0);
star *= 0.55 + 0.45 * sin(Time * (2.0 + r2 * 4.0) + r * 40.0);
col += star * float3(1.0, 0.86, 1.0) * (1.6 + 2.5 * High);
float3 sd = normalize(SunDir.xyz);
float ang = acos(clamp(dot(d, sd), -1.0, 1.0));
float disc = smoothstep(SunSize, SunSize * 0.96, ang);
if (Moon > 0.5)
{
    float3 side = normalize(cross(sd, float3(0, 0, 1)));
    float3 upv = cross(side, sd);
    float2 m = float2(dot(d, side), dot(d, upv)) / max(SunSize, 0.001);
    float crater = 0.86 + 0.14 * sin(m.x * 7.0 + sin(m.y * 5.0) * 2.0) * cos(m.y * 6.0);
    col = lerp(col, SunColorA.rgb * crater, disc);
    col += SunColorA.rgb * 0.35 * exp(-ang / (SunSize * 2.2));
}
else
{
    float sunEl = asin(clamp(sd.z, -1.0, 1.0));
    float t = saturate((el - (sunEl - SunSize)) / (2.0 * SunSize));
    float3 sun = lerp(SunColorB.rgb, SunColorA.rgb, t);
    float stripes = t < 0.55 ? step(0.45 - t * 0.5, frac(t * 13.0 - Time * 0.1)) : 1.0;
    col = lerp(col, sun, disc * stripes);
    col += SunColorB.rgb * 0.45 * exp(-ang / (SunSize * 1.6)) * (0.8 + 0.5 * Bass);
}
return col;
"""

GRID_HLSL = r"""
float2 p = WorldPos.xy;
float2 q = (p + float2(Time * Speed, 0.0)) / GridSize;
float2 gq = abs(frac(q) - 0.5);
float2 w = fwidth(q);
float lx = 1.0 - smoothstep(0.018 - w.x, 0.018 + w.x, 0.5 - gq.x);
float ly = 1.0 - smoothstep(0.018 - w.y, 0.018 + w.y, 0.5 - gq.y);
float l = max(lx, ly) * saturate(1.0 - max(w.x, w.y) * 3.0);
float dist = length(p - ObjPos.xy);
float fade = exp(-dist / Fade);
float3 lineCol = lerp(Accent2.rgb, Accent.rgb, saturate(dist / 2500.0));
float3 col = Base.rgb + lineCol * l * (1.4 + 3.0 * Beat) * fade;
col += Accent.rgb * 0.10 * exp(-dist / 900.0) * (0.4 + Bass);
col = lerp(col, Horizon.rgb, saturate(dist / (Fade * 2.5)));
return col;
"""

LED_HLSL = r"""
float2 cell = floor(WorldPos.xy / CellSize);
float2 f = frac(WorldPos.xy / CellSize) - 0.5;
float h1 = frac(sin(dot(cell, float2(12.9898, 78.233))) * 43758.5453);
float h2 = frac(sin(dot(cell, float2(7.13, 157.3))) * 9124.231);
float on = step(h1, 0.15 + 0.85 * Bass) * (0.35 + 0.65 * Beat);
float3 tint = h2 < 0.33 ? Accent.rgb : (h2 < 0.66 ? Accent2.rgb : Accent3.rgb);
float edge = smoothstep(0.5, 0.42, max(abs(f.x), abs(f.y)));
float wave = 0.5 + 0.5 * sin(Time * 2.0 + h1 * 6.283);
return Base.rgb + tint * edge * (on * 3.0 + 0.10 * wave);
"""

NEON_HLSL = r"""
float e = dot(Band.rgb, float3(Bass, Mid, High)) + Band.a * Beat;
return Color.rgb * Intensity * (1.0 + React * e);
"""

ORB_HLSL = r"""
float f = saturate(abs(dot(normalize(CamVec), normalize(Normal))));
float core = pow(f, 1.6);
return Color.rgb * Intensity * (0.25 + core) * (1.0 + React * (Beat + 0.5 * Bass));
"""

LASER_HLSL = r"""
float f = saturate(abs(dot(normalize(CamVec), normalize(Normal))));
return Color.rgb * Intensity * pow(f, 2.5) * (0.25 + 1.2 * Beat + 0.6 * Bass);
"""

SILHOUETTE_HLSL = r"""
float f = 1.0 - saturate(abs(dot(normalize(CamVec), normalize(Normal))));
return Base.rgb + Accent.rgb * pow(f, 3.0) * (1.2 + Bass);
"""

DISCO_HLSL = r"""
float3 n = normalize(Normal);
float2 uv = float2(atan2(n.y, n.x) * 6.0, asin(clamp(n.z, -1.0, 1.0)) * 6.0);
float2 c = floor(uv * 3.0);
float h = frac(sin(dot(c, float2(12.9898, 78.233))) * 43758.5453);
float sparkle = pow(saturate(sin(Time * (3.0 + h * 5.0) + h * 30.0)), 12.0);
return float3(0.04, 0.03, 0.06) + lerp(Accent.rgb, float3(1, 1, 1), h) * sparkle * (1.5 + 3.0 * Beat);
"""

# Lit materials: base colour (with variation) and a little self-light against black corners.
VELVET_BASE = r"""
float folds = sin(dot(WorldPos.xyz, FoldAxis.xyz) * FoldFreq + sin(WorldPos.z * 0.013) * 1.5);
return Color.rgb * (0.80 + 0.20 * folds);
"""
VELVET_NORMAL = r"""
float folds = cos(dot(WorldPos.xyz, FoldAxis.xyz) * FoldFreq + sin(WorldPos.z * 0.013) * 1.5);
return normalize(normalize(Normal) + normalize(FoldAxis.xyz) * folds * 0.55);
"""
VELVET_EMISSIVE = r"""
float f = 1.0 - saturate(abs(dot(normalize(CamVec), normalize(Normal))));
return Sheen.rgb * pow(f, 2.5) * SheenStrength + Color.rgb * Ambient;
"""
WOOD_BASE = r"""
float grain = sin(WorldPos.x * 0.09 + sin(WorldPos.y * 0.017 + WorldPos.z * 0.011) * 7.0);
float ring = 0.5 + 0.5 * sin(grain * 3.0);
return Color.rgb * (0.70 + 0.30 * ring);
"""
SELF_LIGHT = r"""
return Color.rgb * Ambient;
"""
STONE_BASE = r"""
float2 tile = floor(WorldPos.xy / 60.0);
float h = frac(sin(dot(tile, float2(12.9898, 78.233))) * 43758.5453);
float vein = pow(saturate(sin(WorldPos.x * 0.03 + sin(WorldPos.y * 0.021) * 4.0)), 18.0);
float2 f = abs(frac(WorldPos.xy / 60.0) - 0.5);
float seam = smoothstep(0.49, 0.5, max(f.x, f.y));
return Color.rgb * (0.88 + 0.12 * h) * (1.0 - 0.25 * vein) * (1.0 - 0.35 * seam);
"""

UI_PANEL = r"""
float2 uv = UV;
float2 p = (uv - 0.5) * float2(Aspect, 1.0);
float2 hs = float2(Aspect * 0.5 - 0.015, 0.5 - 0.025);
float r = 0.06;
float2 q = abs(p) - (hs - r);
float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
float aa = fwidth(d) * 1.2 + 1e-5;
float inside = 1.0 - smoothstep(-aa, aa, d);
float3 bg = lerp(float3(0.020, 0.006, 0.048), float3(0.058, 0.012, 0.105), uv.y);
bg += Accent.rgb * 0.16 * exp(-length((uv - float2(0.06, 0.0)) * float2(Aspect, 1.0)) * 2.2);
bg += Accent2.rgb * 0.09 * exp(-length((uv - float2(1.0, 1.05)) * float2(Aspect, 1.0)) * 2.0);
float2 g = p * 70.0;
float2 cell = frac(g) - 0.5;
float dens = saturate(1.1 - length(uv - float2(0.0, 1.0)) * 1.5) + saturate(1.0 - length(uv - float2(1.0, 0.0)) * 1.7);
float dotm = (1.0 - smoothstep(dens * 0.42 - 0.06, dens * 0.42, length(cell))) * step(0.02, dens);
bg += Accent.rgb * 0.08 * dotm;
float2 sg = p * 7.0;
float2 sc = floor(sg);
float2 sf = frac(sg) - 0.5;
float rr = frac(sin(dot(sc, float2(12.9898, 78.233))) * 43758.5453);
float2 sp = sf - (float2(frac(rr * 13.7), frac(rr * 91.3)) - 0.5) * 0.6;
float tw = 0.5 + 0.5 * sin(Time * (1.5 + rr * 3.0) + rr * 20.0);
float spark = (exp(-abs(sp.x) * 60.0) * exp(-abs(sp.y) * 7.0) + exp(-abs(sp.y) * 60.0) * exp(-abs(sp.x) * 7.0)) * step(0.70, rr) * tw;
bg += lerp(Accent.rgb, float3(1, 1, 1), 0.5) * spark * 0.55;
float t = atan2(p.y, p.x) / 6.2832 + 0.5 + Time * 0.03;
float3 edgeCol = lerp(lerp(Accent.rgb, Accent3.rgb, sin(t * 6.2832) * 0.5 + 0.5), Accent2.rgb, (cos(t * 12.566) * 0.5 + 0.5) * 0.5);
float edge = exp(-abs(d) * 200.0) * (1.0 + 0.6 * Beat);
float glow = exp(-max(d, 0.0) * 45.0) * 0.55;
float3 col = bg * inside + edgeCol * (edge * 1.7 + glow * (1.0 - inside));
return float4(col, saturate(inside * 0.93 + edge + glow * (1.0 - inside)));
"""

UI_PILL = r"""
float2 p = (UV - 0.5) * float2(Aspect, 1.0);
float r = 0.44;
float2 hs = float2(Aspect * 0.5 - 0.06, 0.44);
float2 q = abs(p) - (hs - r);
float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
float aa = fwidth(d) * 1.2 + 1e-5;
float inside = 1.0 - smoothstep(-aa, aa, d);
float3 base = lerp(float3(0.06, 0.02, 0.13), float3(0.12, 0.04, 0.22), 1.0 - UV.y);
float3 act = lerp(Accent.rgb, Accent3.rgb, UV.x) * (0.75 + 0.25 * (1.0 - UV.y));
float3 c = lerp(base, act, Active);
c += (Accent.rgb * 0.30 + 0.06) * Hover * (1.0 - Active) + 0.12 * Hover * Active;
c *= 1.0 - 0.25 * Pressed;
c += 0.10 * smoothstep(0.0, -0.3, p.y);
float3 edgeCol = lerp(Accent.rgb, Accent2.rgb, UV.x);
float edge = exp(-abs(d) * 150.0) * (0.55 + 0.45 * max(Hover, Active) + Focus);
float glow = exp(-max(d, 0.0) * 28.0) * (0.12 + 0.6 * max(Hover, Focus));
float3 col = c * inside + edgeCol * (edge + glow * (1.0 - inside));
return float4(col, saturate(inside * 0.96 + edge + glow * (1.0 - inside)));
"""

UI_HEX = r"""
float2 p = (UV - 0.5) * float2(1.1547, 1.0) * 2.0;
float rr = 0.88;
float3 k = float3(-0.866025404, 0.5, 0.577350269);
float2 a = abs(p);
a -= 2.0 * min(dot(k.xy, a), 0.0) * k.xy;
a -= float2(clamp(a.x, -k.z * rr, k.z * rr), rr);
float d = length(a) * sign(a.y);
float aa = fwidth(d) * 1.5 + 1e-5;
float inside = 1.0 - smoothstep(-aa, aa, d);
float2 iuv = (UV - 0.5) * (0.94 - 0.07 * Hover) * Crop.xy + 0.5 + Crop.zw;
float3 img = Image.Sample(ImageSampler, iuv).rgb;
float3 fallback = lerp(Accent.rgb * 0.30, Accent2.rgb * 0.20, UV.y);
float3 pic = lerp(fallback, img, HasImage) * (0.78 + 0.32 * Hover);
pic *= 1.0 - 0.35 * smoothstep(-0.25, 0.0, d);
float3 edgeCol = lerp(Accent.rgb, Accent2.rgb, UV.y);
float edge = exp(-abs(d) * 80.0) * (0.9 + 1.3 * Hover + 0.8 * Active);
float glow = exp(-max(d, 0.0) * 12.0) * (0.22 + 0.8 * Hover);
float3 col = pic * inside + edgeCol * (edge + glow * (1.0 - inside));
return float4(col, saturate(inside + edge + glow * (1.0 - inside) * 0.9));
"""

PINK = (1.0, 0.06, 0.42)
MAGENTA = (0.62, 0.02, 1.0)
VIOLET = (0.18, 0.05, 1.0)
CYAN = (0.05, 0.62, 1.0)
LILAC = (0.55, 0.40, 1.0)
WARM = (1.0, 0.45, 0.12)


def props(obj, **values):
    for key, value in values.items():
        obj.set_editor_property(key, value)
    return obj


def color(rgb, a=1.0):
    return unreal.LinearColor(rgb[0], rgb[1], rgb[2], a)


def load_or_create(path, cls, factory):
    folder, name = path.rsplit('/', 1)
    if LIB.does_asset_exist(path):
        obj = unreal.load_asset(path)
        if isinstance(obj, cls):
            return obj
        assert LIB.delete_asset(path), path
    return TOOLS.create_asset(name, folder, cls, factory)


def data(path, cls):
    return load_or_create(path, cls, props(unreal.DataAssetFactory(), data_asset_class=cls))


def save(obj):
    assert LIB.save_loaded_asset(obj, only_if_is_dirty=False), obj.get_path_name()


def material(name, outputs, inputs, unlit=True, blend='opaque', two_sided=False, ui=False, ism=False, world_normal=False, roughness=0.6, specular=0.5):
    """outputs: {MaterialProperty: (hlsl, 'float3'|'float4')}; a float4 output on emissive also feeds opacity."""
    mat = load_or_create(BASE + '/Materials/' + name, unreal.Material, unreal.MaterialFactoryNew())
    EDIT.delete_all_material_expressions(mat)
    props(mat, two_sided=two_sided, shading_model=unreal.MaterialShadingModel.MSM_UNLIT if unlit else unreal.MaterialShadingModel.MSM_DEFAULT_LIT,
          blend_mode={'opaque': unreal.BlendMode.BLEND_OPAQUE, 'translucent': unreal.BlendMode.BLEND_TRANSLUCENT,
                      'additive': unreal.BlendMode.BLEND_ADDITIVE}[blend])
    if ui:
        props(mat, material_domain=unreal.MaterialDomain.MD_UI)
    if world_normal:
        props(mat, tangent_space_normal=False)
    if ism:
        props(mat, used_with_instanced_static_meshes=True)
    nodes = {}
    spec_kind = {key: spec[0] for key, spec in inputs.items()}
    y = 0
    for key, spec in inputs.items():
        kind = spec[0]
        if kind == 'time':
            node = EDIT.create_material_expression(mat, unreal.MaterialExpressionTime, -1400, y)
        elif kind == 'uv':
            node = EDIT.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1400, y)
        elif kind == 'world':
            node = EDIT.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1400, y)
        elif kind == 'object':
            node = EDIT.create_material_expression(mat, unreal.MaterialExpressionObjectPositionWS, -1400, y)
        elif kind == 'camera':
            node = EDIT.create_material_expression(mat, unreal.MaterialExpressionCameraVectorWS, -1400, y)
        elif kind == 'normal':
            node = EDIT.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -1400, y)
        elif kind == 'music':
            node = props(EDIT.create_material_expression(mat, unreal.MaterialExpressionCollectionParameter, -1400, y),
                         collection=collection, parameter_name=key)
        elif kind == 'scalar':
            node = props(EDIT.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1400, y), parameter_name=key, default_value=float(spec[1]))
        elif kind == 'vector':
            node = props(EDIT.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -1400, y), parameter_name=key,
                         default_value=unreal.LinearColor(*spec[1]))
        elif kind == 'texture':
            node = props(EDIT.create_material_expression(mat, unreal.MaterialExpressionTextureObjectParameter, -1400, y), parameter_name=key,
                         texture=unreal.load_asset(spec[1]))
            if ui:
                props(node, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        else:
            raise ValueError(kind)
        nodes[key] = node
        y += 140
    for index, (prop, (code, kind)) in enumerate(outputs.items()):
        custom = EDIT.create_material_expression(mat, unreal.MaterialExpressionCustom, -500, index * 300)
        props(custom, code=code, output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT4 if kind == 'float4' else unreal.CustomMaterialOutputType.CMOT_FLOAT3,
              inputs=[props(unreal.CustomInput(), input_name=key) for key in nodes])
        for key, node in nodes.items():
            assert EDIT.connect_material_expressions(node, 'RGBA' if spec_kind[key] == 'vector' else '', custom, key), (name, key)
        if kind == 'float4':
            rgb = props(EDIT.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -200, index * 300), r=True, g=True, b=True, a=False)
            alpha = props(EDIT.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -200, index * 300 + 120), r=False, g=False, b=False, a=True)
            assert EDIT.connect_material_expressions(custom, '', rgb, '')
            assert EDIT.connect_material_expressions(custom, '', alpha, '')
            assert EDIT.connect_material_property(rgb, '', prop)
            assert EDIT.connect_material_property(alpha, '', unreal.MaterialProperty.MP_OPACITY)
        else:
            assert EDIT.connect_material_property(custom, '', prop)
    if not unlit:
        rough = props(EDIT.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -500, 900), parameter_name='Roughness', default_value=roughness)
        spec_node = props(EDIT.create_material_expression(mat, unreal.MaterialExpressionConstant, -500, 1000), r=specular)
        assert EDIT.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
        assert EDIT.connect_material_property(spec_node, '', unreal.MaterialProperty.MP_SPECULAR)
    EDIT.recompile_material(mat)
    save(mat)
    return mat


def instance(name, parent, vectors=None, scalars=None):
    mi = load_or_create(BASE + '/Materials/Instances/' + name, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    props(mi, parent=parent)
    for key, value in (vectors or {}).items():
        EDIT.set_material_instance_vector_parameter_value(mi, key, color(value[:3], value[3] if len(value) > 3 else 1.0))
    for key, value in (scalars or {}).items():
        EDIT.set_material_instance_scalar_parameter_value(mi, key, float(value))
    EDIT.update_material_instance(mi)
    save(mi)
    return mi


if SHOULD_AUTHOR:
    collection = load_or_create(BASE + '/MPC_Music', unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    collection.set_editor_property('scalar_parameters', [props(unreal.CollectionScalarParameter(), parameter_name=unreal.Name(n), default_value=0.)
                                                          for n in ('Bass', 'Mid', 'High', 'Beat', 'Energy')])
    save(collection)
    MUSIC = {k: ('music',) for k in ('Bass', 'Mid', 'High', 'Beat')}
    EMISSIVE = unreal.MaterialProperty.MP_EMISSIVE_COLOR

    sky = material('M_SynthSky', {EMISSIVE: (SKY_HLSL, 'float3')}, {
        'CamVec': ('camera',), 'Time': ('time',), **MUSIC,
        'Accent': ('vector', (*PINK, 1)), 'Zenith': ('vector', (0.004, 0.002, 0.03, 1)), 'Horizon': ('vector', (0.16, 0.02, 0.22, 1)),
        'Ground': ('vector', (0.01, 0.003, 0.02, 1)), 'SunDir': ('vector', (1, 0, 0.09, 0)), 'SunSize': ('scalar', 0.16),
        'Moon': ('scalar', 0.0), 'Stars': ('scalar', 120.0), 'SunColorA': ('vector', (1.6, 0.9, 0.12, 1)), 'SunColorB': ('vector', (1.8, 0.05, 0.55, 1))},
        two_sided=True)
    grid = material('M_SynthGrid', {EMISSIVE: (GRID_HLSL, 'float3')}, {
        'WorldPos': ('world',), 'ObjPos': ('object',), 'Time': ('time',), **MUSIC,
        'Accent': ('vector', (*PINK, 1)), 'Accent2': ('vector', (*CYAN, 1)), 'Base': ('vector', (0.006, 0.002, 0.016, 1)),
        'Horizon': ('vector', (0.16, 0.02, 0.22, 1)), 'GridSize': ('scalar', 200.0), 'Speed': ('scalar', 120.0), 'Fade': ('scalar', 9000.0)})
    led = material('M_LedFloor', {EMISSIVE: (LED_HLSL, 'float3')}, {
        'WorldPos': ('world',), 'Time': ('time',), **MUSIC, 'CellSize': ('scalar', 100.0),
        'Accent': ('vector', (*PINK, 1)), 'Accent2': ('vector', (*CYAN, 1)), 'Accent3': ('vector', (*MAGENTA, 1)), 'Base': ('vector', (0.004, 0.002, 0.008, 1))})
    neon = material('M_Neon', {EMISSIVE: (NEON_HLSL, 'float3')}, {
        **MUSIC, 'Color': ('vector', (*PINK, 1)), 'Intensity': ('scalar', 8.0), 'Band': ('vector', (0.6, 0.0, 0.0, 0.6)), 'React': ('scalar', 1.0)})
    orb = material('M_NeonOrb', {EMISSIVE: (ORB_HLSL, 'float3')}, {
        'CamVec': ('camera',), 'Normal': ('normal',), **MUSIC, 'Color': ('vector', (*LILAC, 1)), 'Intensity': ('scalar', 6.0), 'React': ('scalar', 0.8)},
        blend='additive')
    laser = material('M_Laser', {EMISSIVE: (LASER_HLSL, 'float3')}, {
        'CamVec': ('camera',), 'Normal': ('normal',), **MUSIC, 'Color': ('vector', (0.1, 1.0, 0.3, 1)), 'Intensity': ('scalar', 10.0)},
        blend='additive', two_sided=True)
    silhouette = material('M_Silhouette', {EMISSIVE: (SILHOUETTE_HLSL, 'float3')}, {
        'CamVec': ('camera',), 'Normal': ('normal',), **MUSIC, 'Base': ('vector', (0.006, 0.002, 0.018, 1)), 'Accent': ('vector', (*MAGENTA, 1))})
    disco = material('M_Disco', {EMISSIVE: (DISCO_HLSL, 'float3')}, {
        'Normal': ('normal',), 'Time': ('time',), **MUSIC, 'Accent': ('vector', (*CYAN, 1))})
    glow = material('M_Glow', {EMISSIVE: (r'return Color.rgb * Intensity;', 'float3')}, {
        'Color': ('vector', (*PINK, 1)), 'Intensity': ('scalar', 1.0)}, ism=True)
    lit_common = {'WorldPos': ('world',), 'CamVec': ('camera',), 'Normal': ('normal',), 'Color': ('vector', (0.2, 0.05, 0.2, 1)), 'Ambient': ('scalar', 0.12)}
    velvet = material('M_Velvet', {unreal.MaterialProperty.MP_BASE_COLOR: (VELVET_BASE, 'float3'), unreal.MaterialProperty.MP_NORMAL: (VELVET_NORMAL, 'float3'),
                                   EMISSIVE: (VELVET_EMISSIVE, 'float3')},
                      {**lit_common, 'FoldAxis': ('vector', (0, 1, 0, 0)), 'FoldFreq': ('scalar', 0.12), 'Sheen': ('vector', (*PINK, 1)), 'SheenStrength': ('scalar', 0.35)},
                      unlit=False, world_normal=True, roughness=0.85, specular=0.3)
    wood = material('M_Wood', {unreal.MaterialProperty.MP_BASE_COLOR: (WOOD_BASE, 'float3'), EMISSIVE: (SELF_LIGHT, 'float3')},
                    lit_common, unlit=False, roughness=0.55)
    lacquer = material('M_Lacquer', {unreal.MaterialProperty.MP_BASE_COLOR: (r'return Color.rgb;', 'float3'), EMISSIVE: (SELF_LIGHT, 'float3')},
                       lit_common, unlit=False, roughness=0.12, specular=0.8)
    stone = material('M_Stone', {unreal.MaterialProperty.MP_BASE_COLOR: (STONE_BASE, 'float3'), EMISSIVE: (SELF_LIGHT, 'float3')},
                     lit_common, unlit=False, roughness=0.35)
    ui_common = {'UV': ('uv',), 'Time': ('time',), 'Accent': ('vector', (*PINK, 1)), 'Accent2': ('vector', (*CYAN, 1)), 'Accent3': ('vector', (*MAGENTA, 1))}
    ui_panel = material('MUI_Panel', {EMISSIVE: (UI_PANEL, 'float4')}, {**ui_common, 'Aspect': ('scalar', 1.7778), 'Beat': ('scalar', 0.0)},
                        blend='translucent', ui=True)
    ui_pill = material('MUI_Pill', {EMISSIVE: (UI_PILL, 'float4')}, {**ui_common, 'Aspect': ('scalar', 4.0), 'Hover': ('scalar', 0.0),
                       'Active': ('scalar', 0.0), 'Focus': ('scalar', 0.0), 'Pressed': ('scalar', 0.0)}, blend='translucent', ui=True)
    ui_hex = material('MUI_Hex', {EMISSIVE: (UI_HEX, 'float4')}, {**ui_common, 'Image': ('texture', '/Engine/EngineResources/DefaultTexture'), 'Crop': ('vector', (0.545, 0.84, 0.0, -0.06)),
                      'HasImage': ('scalar', 0.0), 'Hover': ('scalar', 0.0), 'Active': ('scalar', 0.0)}, blend='translucent', ui=True)

    # Instances used by the environments.
    MI = {}
    def neon_mi(name, rgb, band=(0.6, 0, 0, 0.6), intensity=8.0, react=1.0):
        MI[name] = instance('MI_Neon_' + name, neon, {'Color': rgb, 'Band': band}, {'Intensity': intensity, 'React': react})
    neon_mi('Pink', PINK); neon_mi('Magenta', MAGENTA, (0, 0.6, 0, 0.6)); neon_mi('Cyan', CYAN, (0, 0, 0.7, 0.4))
    neon_mi('Violet', VIOLET, (0.3, 0.3, 0, 0.3)); neon_mi('Warm', WARM, (0.2, 0, 0, 0.2), 6.0, 0.6); neon_mi('White', (1, 0.9, 1), (0, 0, 0.5, 0.5), 4.0)
    neon_mi('PinkSoft', PINK, (0.3, 0, 0, 0.3), 3.0, 0.8)
    MI['OrbLilac'] = instance('MI_Orb_Lilac', orb, {'Color': LILAC}, {'Intensity': 5.0})
    MI['OrbPink'] = instance('MI_Orb_Pink', orb, {'Color': PINK}, {'Intensity': 5.0})
    MI['OrbWarm'] = instance('MI_Orb_Warm', orb, {'Color': (1.0, 0.35, 0.15)}, {'Intensity': 4.0})
    MI['LaserGreen'] = instance('MI_Laser_Green', laser, {'Color': (0.15, 1.0, 0.25)}, {'Intensity': 12.0})
    MI['LaserPink'] = instance('MI_Laser_Pink', laser, {'Color': PINK}, {'Intensity': 12.0})
    MI['LaserCyan'] = instance('MI_Laser_Cyan', laser, {'Color': CYAN}, {'Intensity': 12.0})
    MI['VelvetPlum'] = instance('MI_Velvet_Plum', velvet, {'Color': (0.10, 0.012, 0.10), 'Sheen': PINK}, {'Ambient': 0.18, 'SheenStrength': 0.25, 'FoldFreq': 0.0})
    MI['VelvetCurtainY'] = instance('MI_Velvet_CurtainY', velvet, {'Color': (0.22, 0.010, 0.14), 'Sheen': PINK, 'FoldAxis': (0, 1, 0)}, {'Ambient': 0.20, 'FoldFreq': 0.14})
    MI['VelvetCurtainX'] = instance('MI_Velvet_CurtainX', velvet, {'Color': (0.22, 0.010, 0.14), 'Sheen': PINK, 'FoldAxis': (1, 0, 0)}, {'Ambient': 0.20, 'FoldFreq': 0.14})
    MI['Satin'] = instance('MI_Velvet_Satin', velvet, {'Color': (0.40, 0.04, 0.24), 'Sheen': (1, 0.6, 0.9)}, {'Ambient': 0.15, 'FoldFreq': 0.05, 'SheenStrength': 0.5})
    MI['WoodDark'] = instance('MI_Wood_Dark', wood, {'Color': (0.10, 0.04, 0.025)}, {'Ambient': 0.15})
    MI['WoodRed'] = instance('MI_Wood_Red', wood, {'Color': (0.30, 0.03, 0.04)}, {'Ambient': 0.18})
    MI['LacquerWine'] = instance('MI_Lacquer_Wine', lacquer, {'Color': (0.05, 0.006, 0.02)}, {'Ambient': 0.15})
    MI['LacquerBlack'] = instance('MI_Lacquer_Black', lacquer, {'Color': (0.01, 0.008, 0.014)}, {'Ambient': 0.1})
    MI['LacquerSteel'] = instance('MI_Lacquer_Steel', lacquer, {'Color': (0.05, 0.05, 0.07)}, {'Ambient': 0.12, 'Roughness': 0.3})
    MI['Marble'] = instance('MI_Stone_Marble', stone, {'Color': (0.36, 0.32, 0.42)}, {'Ambient': 0.10})
    MI['MarbleDark'] = instance('MI_Stone_Dark', stone, {'Color': (0.10, 0.07, 0.16)}, {'Ambient': 0.12})
    MI['Water'] = instance('MI_Lacquer_Water', lacquer, {'Color': (0.006, 0.004, 0.02)}, {'Ambient': 0.05, 'Roughness': 0.04})
    MI['SkySun'] = instance('MI_Sky_Sunset', sky, {}, {})
    MI['SkyMoon'] = instance('MI_Sky_Moon', sky, {'Accent': LILAC, 'Horizon': (0.10, 0.03, 0.22), 'Zenith': (0.003, 0.002, 0.025),
                                                   'SunDir': (1, 0.45, 0.2, 0), 'SunColorA': (1.5, 1.35, 1.7)},
                             {'Moon': 1.0, 'SunSize': 0.13, 'Stars': 150.0})
    MI['Grid'] = instance('MI_Grid_Drive', grid, {}, {})
    MI['SilhouetteMagenta'] = instance('MI_Silhouette_Magenta', silhouette, {}, {})
    MI['SilhouetteLilac'] = instance('MI_Silhouette_Lilac', silhouette, {'Accent': LILAC}, {})

    # ---------------------------------------------------------------------------------- font
    faces = []
    for face_name in ('Regular', 'Bold', 'ExtraBold', 'Black'):
        # Not an import task: the font importer needs Slate, which this commandlet does not have.
        face = unreal.GratiaExperienceToolsLibrary.import_font_face(str(FONTS / f'Nunito-{face_name}.ttf'), f'{BASE}/Font/Nunito-{face_name}')
        assert isinstance(face, unreal.FontFace), face_name
        save(face)
        faces.append(face)
    font = load_or_create(BASE + '/Font/F_Nunito', unreal.Font, unreal.FontFactory())
    assert unreal.GratiaExperienceToolsLibrary.build_composite_font(font, [unreal.Name(n) for n in ('Regular', 'Bold', 'ExtraBold', 'Black')], faces)
    save(font)

    # ---------------------------------------------------------------------------------- music
    def split_channels(path):
        with wave.open(str(path), 'rb') as source:
            assert source.getnchannels() == 2 and source.getsampwidth() == 2, path
            rate = source.getframerate()
            pcm = array.array('h', source.readframes(source.getnframes()))
        for suffix, channel in (('_L', pcm[0::2]), ('_R', pcm[1::2])):
            target = path.with_name(path.stem + suffix + '.wav')
            if target.is_file() and target.stat().st_mtime >= path.stat().st_mtime:
                continue
            with wave.open(str(target), 'wb') as out:
                out.setnchannels(1); out.setsampwidth(2); out.setframerate(rate); out.writeframes(channel.tobytes())

    def import_sound(path, folder, name, looping):
        task = props(unreal.AssetImportTask(), filename=str(path), destination_path=folder, destination_name=name, automated=True, replace_existing=True, save=True)
        TOOLS.import_asset_tasks([task])
        sound = unreal.load_asset(f'{folder}/{name}')
        assert isinstance(sound, unreal.SoundWave), name
        props(sound, looping=looping)
        save(sound)
        return sound

    vector_type = getattr(unreal, 'Vector4f', unreal.Vector4)

    def track(slug, wav, folder, title, artist, looping):
        split_channels(wav)
        analysis_json = json.loads(wav.with_name(wav.stem + '.analysis.json').read_text(encoding='utf-8'))
        sound = import_sound(wav, folder, 'S_' + slug, looping)
        left = import_sound(wav.with_name(wav.stem + '_L.wav'), folder, 'S_' + slug + '_L', looping)
        right = import_sound(wav.with_name(wav.stem + '_R.wav'), folder, 'S_' + slug + '_R', looping)
        frames = [props(vector_type(), x=f[0], y=f[1], z=f[2], w=f[3]) for f in analysis_json['frames']]
        beats = analysis_json.get('beats')
        if beats is None:  # generated loops: a beat every quarter from the tempo
            period = 60.0 / analysis_json['bpm']
            beats = [round(i * period, 3) for i in range(int(len(frames) / 30 / period))]
        asset = data(f'{folder}/DA_Music_{slug}', unreal.GratiaMusicAnalysis)
        props(asset, sound=sound, left_sound=left, right_sound=right, frames_per_second=30.0, beats_per_minute=float(analysis_json['bpm']),
              frames=frames, beats=[float(b) for b in beats], intro_seconds=float(analysis_json.get('intro', 0.0)),
              outro_seconds=float(analysis_json.get('outro', max(0.0, len(frames) / 30 - 8 * 60.0 / analysis_json['bpm']))),
              title=title, artist=artist)
        save(asset)
        return asset

    generated = {}
    for slug, title in (('GratiaLobby', 'Dreamwave'), ('GratiaNeon', 'Neon Drive'), ('GratiaVelvet', 'Velvet Night'), ('GratiaMoon', 'Moonlight')):
        generated[slug] = track(slug, GENERATED / f'{slug}.wav', BASE + '/Music', title, 'Gratia', slug == 'GratiaLobby')
    USER_TRACKS = [('StayAtYourHouse', 'I Really Want to Stay at Your House', 'Rosa Walton & Hallie Coggins'),
                   ('Addict', 'Addict', 'PiNKII × DAEGHO'), ('TurnItUp', 'TURN IT UP', 'SVRGE'),
                   ('NeverSee', 'Never See', 'SPYRAL · Miku × Brazilian Miku'), ('Brain', 'BRAIN', 'Artemas & Diplo'),
                   ('MechanicalCorpse', 'mechanical corpse', 'tommy. ft. GUMI')]
    user = {}
    for slug, title, artist in USER_TRACKS:
        if (USER / f'{slug}.wav').is_file() and (USER / f'{slug}.analysis.json').is_file():
            user[slug] = track(slug, USER / f'{slug}.wav', USER_BASE, title, artist, False)
    playlist = [user[slug] for slug, _, _ in USER_TRACKS if slug in user] or [generated[s] for s in ('GratiaNeon', 'GratiaVelvet', 'GratiaMoon')]

    attenuation = load_or_create(BASE + '/Music/SA_Speakers', unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
    settings = attenuation.get_editor_property('attenuation')
    props(settings, attenuate=True, spatialize=True, falloff_distance=1800.0, attenuation_shape_extents=unreal.Vector(300, 0, 0),
          distance_algorithm=unreal.AttenuationDistanceModel.NATURAL_SOUND, enable_reverb_send=True,
          non_spatialized_radius_start=60.0, non_spatialized_radius_end=0.0)
    attenuation.set_editor_property('attenuation', settings)
    save(attenuation)

    def reverb(name, **values):
        effect = load_or_create(BASE + '/Music/' + name, unreal.ReverbEffect, unreal.ReverbEffectFactory())
        props(effect, **values)
        save(effect)
        return effect
    reverb_room = reverb('RE_VelvetRoom', density=0.6, diffusion=0.8, gain=0.4, gain_hf=0.6, decay_time=1.1, decay_hf_ratio=0.6, late_gain=1.0)
    reverb_club = reverb('RE_Club', density=1.0, diffusion=1.0, gain=0.5, gain_hf=0.7, decay_time=2.4, decay_hf_ratio=0.7, late_gain=1.4)
    reverb_open = reverb('RE_Open', density=0.3, diffusion=0.5, gain=0.25, gain_hf=0.5, decay_time=0.9, decay_hf_ratio=0.5, late_gain=0.6)
    reverb_pavilion = reverb('RE_Pavilion', density=0.7, diffusion=0.7, gain=0.32, gain_hf=0.55, decay_time=1.8, decay_hf_ratio=0.6, late_gain=1.0)

    # ---------------------------------------------------------------------------------- environments
    # Scenes are built from Fab packs by setup_fab_environments.py (run before this script); the
    # generated rooms of the first ViRo pass were replaced at the user's request on 7 October 2026.
    SPHERE = unreal.load_asset('/Engine/BasicShapes/Sphere')
    CYLINDER = unreal.load_asset('/Engine/BasicShapes/Cylinder')
    PLANE = unreal.load_asset('/Engine/BasicShapes/Plane')
    fab = json.loads((OUT / 'fab_environments.json').read_text(encoding='utf-8'))['scenes']
    environments = {name: scene['environment'] for name, scene in fab.items()}
    backdrops = {name: [unreal.load_asset(path) for path in scene['backdrops']] for name, scene in fab.items()}
    assert all(all(backdrops[name]) for name in backdrops), 'A Fab backdrop level is missing; run setup_fab_environments.py'

    # ---------------------------------------------------------------------------------- library
    profile = unreal.load_asset('/Game/Characters/Profiles/DA_Gratia')
    performances = profile.get_editor_property('performance_clips')
    entries = []

    def entry(id, title, description, env, accent, music, music_volume, reverb_effect, performance=''):
        obj = props(unreal.GratiaSceneEntry(), id=unreal.Name(id), title=title, description=description, environment=unreal.load_asset(environments[env]),
                    backdrops=backdrops[env], accent=color(accent), performance=unreal.Name(performance), music_volume=music_volume, reverb=reverb_effect)
        if music:
            obj.set_editor_property('music', music)
        preview = f'{BASE}/Previews/T_{id}'
        if LIB.does_asset_exist(preview):
            obj.set_editor_property('thumbnail', unreal.load_asset(preview))
        entries.append(obj)
        return obj

    pick = lambda slug, fallback: user.get(slug, generated[fallback])
    # Player-facing names of the profile's performance clips (the clip names are technical).
    SHOW_CARDS = {'Idle ZZZ': ('Позирование', 'Шоу · игривая стойка в гостевом доме'),
                  'KM466 Full': ('Ночь на площади', 'Шоу 9,5 мин · с партнёром, вид его глазами')}
    entry('WabiSabi', 'Гостевой дом', 'Ваби-саби: дерево, лён и дневной свет', 'WabiSabi', (0.95, 0.62, 0.38), pick('MechanicalCorpse', 'GratiaMoon'), 0.8, reverb_room)
    entry('SoulCity', 'Ночной квартал', 'Площадь среди трущоб, неон и огни окон', 'SoulCity', CYAN, pick('NeverSee', 'GratiaNeon'), 0.9, reverb_open)
    entry('SoulCityRain', 'Дождливая ночь', 'Та же площадь под ливнем', 'SoulCityRain', LILAC, pick('Brain', 'GratiaMoon'), 0.9, reverb_open)
    performance_analysis = unreal.load_asset(BASE + '/DA_KM466Analysis') if LIB.does_asset_exist(BASE + '/DA_KM466Analysis') else None
    for index, clip in enumerate(performances):
        name = str(clip.get_editor_property('name'))
        if not (clip.get_editor_property('clip') or clip.get_editor_property('segments')):
            continue
        # A performance with a partner lying on the floor needs the open plaza; the furnished guesthouse
        # only has a 1.6 m clear strip, enough for one standing character.
        env = 'SoulCity' if clip.get_editor_property('scene').get_editor_property('partner_mesh') else 'WabiSabi'
        title, description = SHOW_CARDS.get(name, (name, 'Шоу · управление воспроизведением'))
        item = entry('Performance_' + str(index), title, description, env, (0.75, 0.3, 1.0), None, 0.8,
                     reverb_room if env == 'WabiSabi' else reverb_open, name)
        if performance_analysis and clip.get_editor_property('scene').get_editor_property('music') == performance_analysis.get_editor_property('sound'):
            item.set_editor_property('performance_music', performance_analysis)
    library = data(BASE + '/DA_SceneLibrary', unreal.GratiaSceneLibrary)
    props(library, scenes=entries, playlist=playlist, lobby_music=generated['GratiaLobby'], lobby_music_volume=0.55, crossfade_seconds=6.0,
          speaker_attenuation=attenuation, audio_collection=collection, loading_sky_material=MI['SkySun'], glow_material=glow,
          grid_material=MI['Grid'], sphere_mesh=SPHERE, cylinder_mesh=CYLINDER, plane_mesh=PLANE, font=font,
          panel_material=ui_panel, hex_material=ui_hex, pill_material=ui_pill, min_loading_seconds=2.2, fade_seconds=0.5)
    save(library)

    assert LEVELS.load_level('/Game/Gratia/Maps/L_Stage1')
    runtime = [actor for actor in ACTORS.get_all_level_actors() if isinstance(actor, unreal.GratiaStage1Runtime)]
    assert len(runtime) == 1, 'An explicit unique runtime actor is required'
    runtime[0].get_editor_property('scene_director').set_editor_property('library', library)
    for actor in ACTORS.get_all_level_actors():
        if actor.get_actor_label() in ('Floor_6m', 'BackWall', 'LeftWall', 'RightWall', 'ScaleCube_Exactly1m', 'HeightMarker_165cm', 'Stage1Instructions',
                                       'ScaleCubeLabel', 'HeightMarkerLabel', 'Stage1Sun', 'Stage1Sky'):
            tags = list(actor.get_editor_property('tags'))
            if unreal.Name('GratiaStudio') not in tags:
                tags.append(unreal.Name('GratiaStudio'))
            actor.set_editor_property('tags', tags)
    assert LEVELS.save_current_level()
    # Assets of the first experience pass, replaced by the ones above.
    for old in ('Environments/L_Atrium', 'Environments/L_PulseStudio', 'Environments/L_VelvetRoom', 'Environments/L_NeonHorizon',
                'Environments/L_LaserClub', 'Environments/L_MoonPavilion', 'Materials/M_LoadingSky', 'Materials/M_Floor', 'Materials/M_Architecture',
                'Materials/M_ReactiveTrim', 'S_Ambient', 'DA_AmbientAnalysis'):
        if LIB.does_asset_exist(BASE + '/' + old):
            LIB.delete_asset(BASE + '/' + old)
    LIB.set_metadata_tag(library, 'GratiaExperienceAuthoring', AUTHORING_HASH)
    LIB.set_metadata_tag(library, 'GratiaExperienceInputs', INPUT_HASH)
    save(library)
    report = dict(library=library.get_path_name(), scenes=[str(e.get_editor_property('id')) for e in entries],
                  environments=sorted(environments), playlist=[t.get_name() for t in playlist], user_tracks=sorted(user),
                  note='Generated music is original; user tracks are personal-use files kept out of git.', runtime_python=False)
    (OUT / 'scene_assets.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('GRATIA_EXPERIENCE_ASSETS_READY ' + json.dumps(report, ensure_ascii=False))
else:
    unreal.log('GRATIA_EXPERIENCE_ASSETS_READY unchanged authoring=' + AUTHORING_HASH)
