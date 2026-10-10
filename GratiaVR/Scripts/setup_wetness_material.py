"""Wet look for the character: MF_GratiaWetness and its wiring into skin, both clothing layers and hair.

Editor commandlet, run after the C++ editor build (and after the other material setups, it wraps their result):
  UnrealEditor-Cmd GratiaVR.uproject -run=pythonscript -script=Scripts/setup_wetness_material.py -unattended -NullRHI

The character materials are unlit toon (emissive = diffuse x toon light), so the wet look is drawn there too, as the last
step of the emissive:
- the film: a darker colour (Porosity: skin a little, fabric a lot), a sky reflection at grazing angles, a broad soft
  highlight and a hard, stylised (anime) specular spot from the toon key light, and a bright rim band;
- droplets running down the body's up axis in columns, each with its own speed, a slightly wobbling path and a thinning
  trail above it (two projections across the body, so they run on every side), and beads sitting on the skin
  (pre-skinned space: they stay where they are while the body moves); both bend the normal, so every drop catches its own
  highlight, with a darker rim like a refracting bead.
The scalar parameter Wetness (0 dry .. 1 soaked) is per character: UGratiaSceneDirector sets it on the character's
materials (menu "Влажность", scenes with CharacterWetness). Wetness 0 returns the colour unchanged (one uniform branch).
Cost when wet: one custom node of roughly 120 instructions per pixel; no textures.
Overwrites the function it created before; a material already wired is not wired twice.
"""
import json
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[2]
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
MF_PATH = '/Game/Gratia/CharacterMaterials/MF_GratiaWetness'
# Material, porosity (darkening), beads, drops, specular strength.
TARGETS = [
    ('/Game/Gratia/CharacterMaterials/M_Gratia_Body_skin', 0.18, 1.0, 1.0, 1.0),
    ('/Game/Gratia/CharacterMaterials/M_Gratia_Default_cloth_1', 0.38, 0.35, 0.8, 0.8),
    ('/Game/Gratia/CharacterMaterials/M_Gratia_Default_cloth_2', 0.38, 0.35, 0.8, 0.8),
    ('/Game/Gratia/CharacterMaterials/M_Gratia_hair', 0.30, 0.0, 0.0, 0.7),
]

CODE = r'''
struct FGratiaWet
{
    float Hash(float N) { return frac(sin(N) * 43758.5453); }
    float3 Hash3(float3 P)
    {
        P = frac(P * float3(0.1031, 0.1030, 0.0973));
        P += dot(P, P.yxz + 33.33);
        return frac((P.xxy + P.yxx) * P.zyx);
    }
    // One column layer of running drops: x mask, yz slope (across, up) in units of the drop's size.
    float3 Drops(float Lateral, float Up, float Time, float Seed, float Amount)
    {
        const float Width = 1.6;
        float U = Lateral / Width;
        float Column = floor(U);
        float X = (frac(U) - 0.5) * Width;
        float H1 = Hash(Column * 13.17 + Seed), H2 = Hash(Column * 7.71 + Seed * 3.3), H3 = Hash(Column * 3.13 + Seed * 5.7);
        if (H1 > Amount) return 0;
        float Speed = lerp(2.5, 7.0, H2);
        float Cycle = lerp(25.0, 45.0, H3);
        float Above = frac((Up + Time * Speed) / Cycle + H2) * Cycle;
        X -= sin(Up * 0.9 + H3 * 6.2832) * 0.12;
        float Radius = lerp(0.18, 0.3, H3);
        float2 Head = float2(X, (Above - Radius) * 0.8) / Radius;
        float HeadMask = 1.0 - smoothstep(0.8, 1.0, length(Head));
        float Along = (Above - Radius) / lerp(4.0, 12.0, H1 / max(Amount, 0.001));
        float TrailWidth = max(Radius * 0.45 * (1.0 - Along), 0.001);
        float Trail = Along > 0.0 && Along < 1.0 ? (1.0 - smoothstep(TrailWidth * 0.6, TrailWidth, abs(X))) * (1.0 - Along) : 0.0;
        if (HeadMask >= Trail * 0.7) return float3(HeadMask, Head * HeadMask);
        return float3(Trail * 0.7, X / TrailWidth * Trail * 0.5, 0.0);
    }
};
if (Wet <= 0.001) return Color;
FGratiaWet G;
float3 Normal = normalize(N);
float3 RestNormal = normalize(PN);
float3 ToEye = normalize(V);
// Beads: one per 0.9 cm cell of the pre-skinned surface, kept inside its cell.
float3 Cell = P / 0.9;
float3 Base = floor(Cell);
float3 Random = G.Hash3(Base);
float3 Offset = Cell - (Base + 0.5 + (Random - 0.5) * 0.4);
Offset -= dot(Offset, RestNormal) * RestNormal;
float BeadRadius = lerp(0.12, 0.3, Random.y);
float BeadDistance = length(Offset) / BeadRadius;
float Bead = step(Random.x, Beads * Wet * 0.6) * (1.0 - smoothstep(0.85, 1.0, BeadDistance));
float3 Slope = Offset / BeadRadius * Bead;
// Drops along the body's up axis, in two projections across it (on the sides and on the front/back).
float Across = abs(RestNormal.x) / max(abs(RestNormal.x) + abs(RestNormal.y), 0.001);
float3 DropX = G.Drops(P.y, P.z, T, 17.0, Drops * Wet) * Across;
float3 DropY = G.Drops(P.x, P.z, T, 41.0, Drops * Wet) * (1.0 - Across);
float Drop = max(DropX.x, DropY.x);
float3 Bend = AX * Slope.x + AY * Slope.y + AZ * Slope.z + AY * DropX.y + AX * DropY.y + AZ * (DropX.z + DropY.z);
float Liquid = saturate(max(Drop, Bead));
float3 Wetted = normalize(Normal - Bend * 0.7);
float3 Light = normalize(float3(-0.55, -0.2, 0.811));
float3 Half = normalize(Light + ToEye);
float NH = saturate(dot(Wetted, Half));
float Fresnel = pow(1.0 - saturate(dot(Wetted, ToEye)), 4.0);
float Sharp = smoothstep(0.965, 0.975, NH);
float Broad = pow(NH, 40.0) * 0.45;
float Band = smoothstep(0.35, 0.45, Fresnel) * (1.0 - smoothstep(0.75, 0.9, Fresnel));
float3 Sky = lerp(float3(0.06, 0.06, 0.08), float3(0.55, 0.6, 0.7), saturate(reflect(-ToEye, Wetted).z * 0.5 + 0.5));
float3 Result = Color * lerp(1.0, 1.0 - Porosity, Wet);
Result *= 1.0 - Liquid * 0.15 * (1.0 - Sharp);
Result = lerp(Result, Result * 0.6 + Sky * 0.4, Fresnel * Wet * 0.5);
Result += (Broad * Wet + Sharp * (0.25 * Wet + Liquid)) * Specular;
Result += Band * Wet * 0.06 * Specular;
return Result;
'''


# ------------------------------------------------------------------ function
existing = lib.load_asset(MF_PATH) if lib.does_asset_exist(MF_PATH) else None
custom_nodes = [n for n in mel.get_material_function_expressions(existing) if isinstance(n, unreal.MaterialExpressionCustom)] if existing else []
if custom_nodes:
    # Rebuilding the inputs would give them new ids and cut the materials' links: only the shader code is replaced.
    custom_nodes[0].set_editor_property('code', CODE)
    mel.update_material_function(existing)
    assert lib.save_loaded_asset(existing)
    function = existing
else:
    function = existing or tools.create_asset('MF_GratiaWetness', '/Game/Gratia/CharacterMaterials', unreal.MaterialFunction,
                                              unreal.MaterialFunctionFactoryNew())
    mel.delete_all_material_expressions_in_function(function)
if not custom_nodes:
    function.set_editor_property('description', 'Wet skin/fabric for the unlit toon materials: darker, glossy, running drops and beads. Wetness 0..1.')


    def node(cls, x, y, **props):
        result = mel.create_material_expression_in_function(function, cls, x, y)
        for key, value in props.items():
            result.set_editor_property(key, value)
        return result


    def function_input(name, kind, x, y, preview, priority):
        result = node(unreal.MaterialExpressionFunctionInput, x, y, input_name=name, input_type=kind, sort_priority=priority)
        if preview is not None:
            try:
                result.set_editor_property('preview_value', unreal.Vector4(*preview))
            except Exception:
                value = unreal.Vector4f()
                for name, component in zip(('X', 'Y', 'Z', 'W'), preview):
                    value.set_editor_property(name.lower(), component)
                result.set_editor_property('preview_value', value)
            result.set_editor_property('use_preview_value_as_default', True)
        return result


    SCALAR, VECTOR3 = unreal.FunctionInputType.FUNCTION_INPUT_SCALAR, unreal.FunctionInputType.FUNCTION_INPUT_VECTOR3
    color_in = function_input('Color', VECTOR3, -1200, -400, None, 0)
    porosity_in = function_input('Porosity', SCALAR, -1200, -250, (0.2, 0, 0, 0), 1)
    beads_in = function_input('Beads', SCALAR, -1200, -150, (1, 0, 0, 0), 2)
    drops_in = function_input('Drops', SCALAR, -1200, -50, (1, 0, 0, 0), 3)
    specular_in = function_input('Specular', SCALAR, -1200, 50, (1, 0, 0, 0), 4)
    wet = node(unreal.MaterialExpressionScalarParameter, -1200, 160, parameter_name='Wetness', default_value=0.0, group='Wetness')
    wet_gain = node(unreal.MaterialExpressionScalarParameter, -1200, 260, parameter_name='WetnessSpecular', default_value=1.0, group='Wetness')
    spec = node(unreal.MaterialExpressionMultiply, -950, 60)
    mel.connect_material_expressions(specular_in, '', spec, 'A')
    mel.connect_material_expressions(wet_gain, '', spec, 'B')
    # Pre-skinned position and normal exist only in the vertex shader: interpolated to the pixels.
    position = node(unreal.MaterialExpressionVertexInterpolator, -1200, 360)
    mel.connect_material_expressions(node(unreal.MaterialExpressionPreSkinnedPosition, -1450, 360), '', position, '')
    rest_normal = node(unreal.MaterialExpressionVertexInterpolator, -1200, 440)
    mel.connect_material_expressions(node(unreal.MaterialExpressionPreSkinnedNormal, -1450, 440), '', rest_normal, '')
    normal = node(unreal.MaterialExpressionVertexNormalWS, -1200, 520)
    camera = node(unreal.MaterialExpressionCameraVectorWS, -1200, 600)
    time = node(unreal.MaterialExpressionTime, -1200, 680)
    axes = []
    for index, axis in enumerate(((1, 0, 0), (0, 1, 0), (0, 0, 1))):
        constant = node(unreal.MaterialExpressionConstant3Vector, -1450, 760 + index * 90, constant=unreal.LinearColor(*axis, 1.0))
        transform = node(unreal.MaterialExpressionTransform, -1200, 760 + index * 90,
                         transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
                         transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
        assert mel.connect_material_expressions(constant, '', transform, '')
        axes.append(transform)
    custom = node(unreal.MaterialExpressionCustom, -700, 0, code=CODE, output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                  description='GratiaWetness')
    names = ['Color', 'Wet', 'Porosity', 'Beads', 'Drops', 'Specular', 'P', 'PN', 'N', 'V', 'T', 'AX', 'AY', 'AZ']
    inputs = []
    for name in names:
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name)
        inputs.append(item)
    custom.set_editor_property('inputs', inputs)
    sources = [color_in, wet, porosity_in, beads_in, drops_in, spec, position, rest_normal, normal, camera, time] + axes
    for name, source in zip(names, sources):
        assert mel.connect_material_expressions(source, '', custom, name), name
    output = node(unreal.MaterialExpressionFunctionOutput, -400, 0, output_name='Result')
    assert mel.connect_material_expressions(custom, '', output, '')
    mel.update_material_function(function)
    assert lib.save_loaded_asset(function)

# ------------------------------------------------------------------ wiring
report = []
for path, porosity, beads, drops, specular in TARGETS:
    if not lib.does_asset_exist(path):
        report.append({'material': path, 'skipped': 'missing'})
        continue
    material = lib.load_asset(path)
    calls = [n for n in mel.get_material_expressions(material) if isinstance(n, unreal.MaterialExpressionMaterialFunctionCall)
             and n.get_editor_property('material_function') == function]
    if calls:
        mel.update_material_function(function, material)
        mel.recompile_material(material)
        assert lib.save_loaded_asset(material)
        report.append({'material': path, 'already_wired': True})
        continue
    emissive = mel.get_material_property_input_node(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if not emissive:
        # Already wired on an earlier run (the property reads back empty through the function call's output): the
        # call nodes relink the rebuilt function's inputs by name, then the material recompiles.
        mel.update_material_function(function, material)
        mel.recompile_material(material)
        assert lib.save_loaded_asset(material)
        report.append({'material': path, 'rewired': False})
        continue
    # The emissive may come from a named output of a function call (the shaft press): keep exactly that output.
    output = mel.get_material_property_input_node_output_name(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    call = mel.create_material_expression(material, unreal.MaterialExpressionMaterialFunctionCall, 900, 0)
    call.set_editor_property('material_function', function)
    mel.update_material_function(function, material)
    # The function's inputs, in sort order: Color, Porosity, Beads, Drops, Specular.
    assert mel.connect_material_expressions(emissive, output, call, 'Color'), (path, output)
    for value, pin, y in ((porosity, 'Porosity', 120), (beads, 'Beads', 200), (drops, 'Drops', 280), (specular, 'Specular', 360)):
        constant = mel.create_material_expression(material, unreal.MaterialExpressionConstant, 700, y)
        constant.set_editor_property('r', value)
        assert mel.connect_material_expressions(constant, '', call, pin), (path, pin)
    assert mel.connect_material_property(call, 'Result', unreal.MaterialProperty.MP_EMISSIVE_COLOR), path
    mel.recompile_material(material)
    assert lib.save_loaded_asset(material)
    report.append({'material': path, 'porosity': porosity, 'beads': beads, 'drops': drops, 'specular': specular})
out = ROOT / 'evidence' / '10'
out.mkdir(parents=True, exist_ok=True)
(out / 'wetness_material.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
unreal.log_warning('GRATIA_WETNESS_MATERIAL ' + json.dumps(report))
