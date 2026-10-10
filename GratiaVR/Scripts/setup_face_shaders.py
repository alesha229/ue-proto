"""Anime face and skin effects for the procedural face (UGratiaProceduralFace): material functions + wiring.

Editor commandlet, run after the C++ editor build (UnrealEditor-Cmd -run=pythonscript -script=...). Creates
- /Game/Gratia/CharacterMaterials/MF_GratiaFaceFX: manga blush (soft cheeks + diagonal hatching strokes),
  tears in the eye corners, anime sweat drops on the temples (face); flush, sweat beads and goosebumps (body);
  mouth slide onto the cheek in 3/4 view (world position offset, face);
- /Game/Gratia/CharacterMaterials/MF_GratiaEyeFX: pupil dilation (iris UV remap around the pupil) and
  emissive heart pupils;
and wires them into M_Gratia_Gratia_face, M_Gratia_Body_skin and M_Gratia_Gratia_eyes after their current
emissive (Unlit toon: emissive is the final colour). Re-running rebuilds the functions; wired materials stay wired.

Runtime parameters (written per mesh by UGratiaProceduralFace through SetScalarParameterValueOnMaterials):
GratiaBlush, GratiaSweat, GratiaGoosebumps, GratiaTears, GratiaPupilScale, GratiaHeartPupils,
GratiaMouthShiftCm (scalars) and GratiaHeadRight (vector, world). Layout parameters (UV positions on the
Gratia face/eye textures, measured from textures/face_diff.png and eyes_diff.png) are editable per material:
GratiaCheeks, GratiaCheekShape, GratiaTearUV, GratiaSweatUV, GratiaMouthUV, GratiaPupil.
"""
import json
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[2]
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
FOLDER = "/Game/Gratia/CharacterMaterials"
FACE = FOLDER + "/M_Gratia_Gratia_face"
BODY = FOLDER + "/M_Gratia_Body_skin"
EYES = FOLDER + "/M_Gratia_Gratia_eyes"

SCALARS = {  # runtime values; defaults are the neutral face
    "GratiaBlush": 0.0, "GratiaSweat": 0.0, "GratiaGoosebumps": 0.0, "GratiaTears": 0.0,
    "GratiaPupilScale": 1.0, "GratiaHeartPupils": 0.0, "GratiaMouthShiftCm": 0.0,
}
VECTORS = {
    "GratiaHeadRight": (1.0, 0.0, 0.0, 0.0),
    # Layout on face_diff (UV, v down): cheeks L xy / R zw; cheek radii xy, hatching stroke count z, stroke width w.
    "GratiaCheeks": (0.345, 0.565, 0.655, 0.565),
    "GratiaCheekShape": (0.075, 0.04, 70.0, 0.22),
    # Tear start under the outer eye corners; temple sweat drops (L xy, R zw).
    "GratiaTearUV": (0.305, 0.525, 0.695, 0.525),
    "GratiaSweatUV": (0.215, 0.405, 0.79, 0.425),
    # Mouth centre xy and slide mask radii zw.
    "GratiaMouthUV": (0.5, 0.607, 0.075, 0.05),
    # eyes_diff: pupil centre xy, iris radius z, pupil radius w.
    "GratiaPupil": (0.5, 0.5, 0.42, 0.07),
    # Body: sweat bead grid density x, goosebump grid density y (UV cells).
    "GratiaBodyDetail": (160.0, 520.0, 0.0, 0.0),
}

FACE_CODE = """
float3 Col = C;
float B = saturate(Blush);
// Soft cheeks.
float M = 0;
[unroll] for (int s = 0; s < 2; ++s)
{
    float2 Ctr = s == 0 ? Cheeks.xy : Cheeks.zw;
    float2 D = (UV - Ctr) / max(Shape.xy, 1e-3);
    M = max(M, saturate(1 - dot(D, D)));
}
M = M * M * (3 - 2 * M);
Col = lerp(Col, Col * float3(1.0, 0.42, 0.45) * 1.08, M * B * 0.75);
// Manga hatching: diagonal strokes in the cheek core above a medium blush.
float Stroke = abs(frac((UV.x * 0.6 - UV.y) * Shape.z) - 0.5) * 2;
float Line = 1 - smoothstep(Shape.w, Shape.w + 0.1, Stroke);
float Core = saturate((M - 0.35) / 0.4);
Col = lerp(Col, float3(0.8, 0.12, 0.2), Line * Core * saturate((B - 0.35) / 0.4) * 0.7);
// Tears: a glossy streak growing down from the outer eye corners, with a bead at its end.
float T = saturate(Tears);
if (T > 0.001)
{
    [unroll] for (int s = 0; s < 2; ++s)
    {
        float2 Ctr = s == 0 ? TearUV.xy : TearUV.zw;
        float Len = 0.012 + 0.07 * T;
        float2 D = UV - Ctr;
        float Along = saturate(D.y / Len);
        float Width = 0.006 + 0.004 * Along;
        float Streak = (D.y > -0.004 && D.y < Len) ? 1 - smoothstep(Width * 0.6, Width, abs(D.x)) : 0;
        float Bead = 1 - smoothstep(0.008, 0.011, length(D - float2(0, Len)));
        float Mask = max(Streak * 0.75, Bead) * saturate(T * 2);
        float Glint = 1 - smoothstep(0.0, 0.004, length(D - float2(-0.003, Len - 0.003)));
        Col = lerp(Col, float3(0.72, 0.86, 1.0) * 1.2, Mask * 0.7) + Glint * Bead * saturate(T * 2) * 0.8;
    }
}
// Anime sweat drops on the temples (teardrop with outline and highlight).
float W = saturate((Sweat - 0.25) / 0.5);
if (W > 0.001)
{
    [unroll] for (int s = 0; s < 2; ++s)
    {
        float2 Ctr = s == 0 ? SweatUV.xy : SweatUV.zw;
        float R = 0.018 * (0.6 + 0.4 * W) * (s == 0 ? 0.8 : 1.0);
        float2 P = (UV - Ctr) / R;
        P.y -= 0.35;
        float Drop = length(float2(P.x * (1 + saturate(-P.y) * 1.6), P.y));
        float Fill = 1 - smoothstep(0.92, 1.0, Drop);
        float Rim = Fill * smoothstep(0.7, 0.9, Drop);
        float Hi = 1 - smoothstep(0.12, 0.22, length(P - float2(-0.3, 0.1)));
        float3 Water = lerp(float3(0.75, 0.9, 1.0), float3(0.25, 0.55, 0.95), Rim);
        Col = lerp(Col, Water + Hi * 0.6, Fill * W);
    }
}
return Col;
"""

BODY_CODE = """
float3 Col = C;
// Flush: the whole skin warms up with the blush.
Col = lerp(Col, Col * float3(1.0, 0.6, 0.62) * 1.05, saturate(Blush) * 0.35);
// Sweat beads: one bead per hashed UV cell, more cells light up with more sweat.
float S = saturate(Sweat);
if (S > 0.001)
{
    float2 G = UV * Detail.x;
    float2 Cell = floor(G);
    float2 F = frac(G) - 0.5;
    float H = frac(sin(dot(Cell, float2(12.9898, 78.233))) * 43758.5453);
    float2 O = (float2(frac(H * 7.13), frac(H * 3.71)) - 0.5) * 0.5;
    float R = 0.12 + 0.16 * frac(H * 11.3);
    float On = H < S * 0.55 ? 1 : 0;
    float Bead = On * (1 - smoothstep(R * 0.7, R, length(F - O)));
    float Spec = On * (1 - smoothstep(0.0, R * 0.45, length(F - O - float2(-R * 0.35, -R * 0.35))));
    Col = lerp(Col, Col * 0.92 + 0.08, Bead * 0.5) + Spec * 0.55;
}
// Goosebumps: fine bumps (bright tip, darker rim) over the skin.
float Gb = saturate(Goosebumps);
if (Gb > 0.001)
{
    float2 G = UV * Detail.y;
    float2 F = frac(G) - 0.5;
    float H = frac(sin(dot(floor(G), float2(39.346, 11.135))) * 43758.5453);
    float D = length(F - (H - 0.5) * 0.3);
    float Bump = 1 - smoothstep(0.12, 0.3, D);
    Col *= 1 - 0.09 * Gb * (Bump - 0.5 * (1 - smoothstep(0.0, 0.12, D)));
}
return Col;
"""

MOUTH_CODE = """
float2 D = (UV - Mouth.xy) / max(Mouth.zw, 1e-3);
float M = saturate(1 - dot(D, D));
M = M * M * (3 - 2 * M);
return Right.xyz * Shift * M;
"""

EYE_UV_CODE = """
float2 V = UV - Pupil.xy;
float D = length(V);
float R = max(Pupil.z, 1e-3);
if (D >= R || D < 1e-5) return UV;
float R0 = clamp(Pupil.w, 1e-3, R * 0.9);
float Rs = clamp(R0 * Scale, 0.005, R * 0.9);
// Pupil zone scales by Scale; the iris between the pupil and its rim is compressed/stretched to keep the rim.
float Dn = D < Rs ? D * (R0 / Rs) : lerp(R0, R, (D - Rs) / (R - Rs));
return Pupil.xy + V * (Dn / D);
"""

EYE_COLOR_CODE = """
float3 Col = C;
float K = saturate(Heart);
if (K <= 0.001) return Col;
float Size = max(Pupil.z * 0.55 * Heart, 1e-3);
float2 P = (UV - Pupil.xy) / Size;
P.y = -P.y + 0.55;
P.x = abs(P.x);
float D;
if (P.y + P.x > 1.0) D = sqrt(dot(P - float2(0.25, 0.75), P - float2(0.25, 0.75))) - 0.35355;
else D = sqrt(min(dot(P - float2(0, 1), P - float2(0, 1)), dot(P - 0.5 * max(P.x + P.y, 0), P - 0.5 * max(P.x + P.y, 0)))) * sign(P.x - P.y);
float Mask = 1 - smoothstep(-0.03, 0.03, D);
float Inner = 1 - smoothstep(-0.25, -0.12, D);
float3 Pink = lerp(float3(1.0, 0.18, 0.5), float3(1.0, 0.65, 0.85), Inner * 0.6) * 1.7;
float Glint = 1 - smoothstep(0.05, 0.12, length(P - float2(0.32, 0.72)));
return lerp(Col, Pink, Mask * K) + Glint * Mask * K * 0.8;
"""


def make_function(path, description):
    if lib.does_asset_exist(path):
        function = lib.load_asset(path)
        mel.delete_all_material_expressions_in_function(function)
    else:
        folder, name = path.rsplit("/", 1)
        function = tools.create_asset(name, folder, unreal.MaterialFunction, unreal.MaterialFunctionFactoryNew())
    function.set_editor_property("description", description)
    return function


def builder(function):
    def expr(cls, x, y):
        return mel.create_material_expression_in_function(function, cls, x, y)
    return expr


def function_input(expr, name, kind, priority, x, y):
    node = expr(unreal.MaterialExpressionFunctionInput, x, y)
    node.set_editor_property("input_name", name)
    node.set_editor_property("input_type", kind)
    node.set_editor_property("sort_priority", priority)
    return node


def parameters(expr, names, x, y):
    nodes = {}
    for index, name in enumerate(names):
        if name in SCALARS:
            node = expr(unreal.MaterialExpressionScalarParameter, x, y + index * 90)
            node.set_editor_property("default_value", SCALARS[name])
        else:
            node = expr(unreal.MaterialExpressionVectorParameter, x, y + index * 90)
            node.set_editor_property("default_value", unreal.LinearColor(*VECTORS[name]))
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("group", "Gratia Face")
        nodes[name] = node
    return nodes


def mask(expr, source, channels, x, y):
    node = expr(unreal.MaterialExpressionComponentMask, x, y)
    for channel in "rgba":
        node.set_editor_property(channel, channel in channels)
    # A vector parameter's default output is RGB: a mask that needs alpha takes its RGBA output.
    out = "RGBA" if isinstance(source, unreal.MaterialExpressionVectorParameter) and "a" in channels else ""
    assert mel.connect_material_expressions(source, out, node, "")
    return node


def custom(expr, code, output_type, description, inputs, x, y):
    """inputs: list of (pin name, source node, source output, component mask or None)."""
    node = expr(unreal.MaterialExpressionCustom, x, y)
    pins = []
    for name, _, _, _ in inputs:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", name)
        pins.append(item)
    node.set_editor_property("inputs", pins)
    node.set_editor_property("code", code)
    node.set_editor_property("output_type", output_type)
    node.set_editor_property("description", description)
    for index, (name, source, output, channels) in enumerate(inputs):
        if channels:
            source = mask(expr, source, channels, x - 250, y - 300 + index * 70)
            output = ""
        assert mel.connect_material_expressions(source, output, node, name), (description, name)
    return node


def output(expr, name, source, priority, x, y):
    node = expr(unreal.MaterialExpressionFunctionOutput, x, y)
    node.set_editor_property("output_name", name)
    node.set_editor_property("sort_priority", priority)
    assert mel.connect_material_expressions(source, "", node, "")
    return node


F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
F2 = unreal.CustomMaterialOutputType.CMOT_FLOAT2

# ------------------------------------------------------------------ MF_GratiaFaceFX
face_fx = make_function(FOLDER + "/MF_GratiaFaceFX",
                        "Gratia procedural face: manga blush, tears, sweat drops (face); flush, sweat beads, goosebumps (body); mouth slide.")
expr = builder(face_fx)
color_in = function_input(expr, "Color", unreal.FunctionInputType.FUNCTION_INPUT_VECTOR3, 0, -1600, -300)
uv = expr(unreal.MaterialExpressionTextureCoordinate, -1600, 0)
p = parameters(expr, ["GratiaBlush", "GratiaSweat", "GratiaGoosebumps", "GratiaTears", "GratiaMouthShiftCm",
                      "GratiaHeadRight", "GratiaCheeks", "GratiaCheekShape", "GratiaTearUV", "GratiaSweatUV",
                      "GratiaMouthUV", "GratiaBodyDetail"], -1600, 200)
face_color = custom(expr, FACE_CODE, F3, "GratiaFaceColor", [
    ("C", color_in, "", None), ("UV", uv, "", None), ("Blush", p["GratiaBlush"], "", None),
    ("Sweat", p["GratiaSweat"], "", None), ("Tears", p["GratiaTears"], "", None),
    ("Cheeks", p["GratiaCheeks"], "", "rgba"), ("Shape", p["GratiaCheekShape"], "", "rgba"),
    ("TearUV", p["GratiaTearUV"], "", "rgba"), ("SweatUV", p["GratiaSweatUV"], "", "rgba"),
], -600, -300)
body_color = custom(expr, BODY_CODE, F3, "GratiaBodyColor", [
    ("C", color_in, "", None), ("UV", uv, "", None), ("Blush", p["GratiaBlush"], "", None),
    ("Sweat", p["GratiaSweat"], "", None), ("Goosebumps", p["GratiaGoosebumps"], "", None),
    ("Detail", p["GratiaBodyDetail"], "", "rgba"),
], -600, 300)
mouth = custom(expr, MOUTH_CODE, F3, "GratiaMouthShift", [
    ("UV", uv, "", None), ("Mouth", p["GratiaMouthUV"], "", "rgba"),
    ("Right", p["GratiaHeadRight"], "", "rgb"), ("Shift", p["GratiaMouthShiftCm"], "", None),
], -600, 800)
output(expr, "FaceColor", face_color, 0, 0, -300)
output(expr, "BodyColor", body_color, 1, 0, 300)
output(expr, "MouthOffset", mouth, 2, 0, 800)
mel.update_material_function(face_fx)
assert lib.save_loaded_asset(face_fx, only_if_is_dirty=False)

# ------------------------------------------------------------------ MF_GratiaEyeFX
eye_fx = make_function(FOLDER + "/MF_GratiaEyeFX", "Gratia procedural eyes: pupil dilation (iris UV remap) and heart pupils.")
expr = builder(eye_fx)
eye_color_in = function_input(expr, "Color", unreal.FunctionInputType.FUNCTION_INPUT_VECTOR3, 0, -1400, -200)
eye_uv = expr(unreal.MaterialExpressionTextureCoordinate, -1400, 100)
q = parameters(expr, ["GratiaPupilScale", "GratiaHeartPupils", "GratiaPupil"], -1400, 300)
eye_uv_out = custom(expr, EYE_UV_CODE, F2, "GratiaPupilUV", [
    ("UV", eye_uv, "", None), ("Pupil", q["GratiaPupil"], "", "rgba"), ("Scale", q["GratiaPupilScale"], "", None),
], -500, 100)
eye_color = custom(expr, EYE_COLOR_CODE, F3, "GratiaHeartPupils", [
    ("C", eye_color_in, "", None), ("UV", eye_uv, "", None), ("Pupil", q["GratiaPupil"], "", "rgba"),
    ("Heart", q["GratiaHeartPupils"], "", None),
], -500, -200)
output(expr, "UV", eye_uv_out, 0, 0, 100)
output(expr, "Color", eye_color, 1, 0, -200)
mel.update_material_function(eye_fx)
assert lib.save_loaded_asset(eye_fx, only_if_is_dirty=False)


# ------------------------------------------------------------------ materials
def emissive_source(material):
    node = mel.get_material_property_input_node(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    assert node is not None, (material.get_path_name(), "has no emissive input (expected the Unlit toon colour)")
    name = mel.get_material_property_input_node_output_name(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return node, str(name or "")


def already_wired(node, function):
    return isinstance(node, unreal.MaterialExpressionMaterialFunctionCall) and node.get_editor_property("material_function") == function


def wire_color(material, function, input_name, output_name, x, y):
    node, out = emissive_source(material)
    if already_wired(node, function):
        return None, "already wired"
    call = mel.create_material_expression(material, unreal.MaterialExpressionMaterialFunctionCall, x, y)
    call.set_editor_property("material_function", function)
    assert mel.connect_material_expressions(node, out, call, input_name), material.get_path_name()
    assert mel.connect_material_property(call, output_name, unreal.MaterialProperty.MP_EMISSIVE_COLOR), material.get_path_name()
    return call, "wired"


report = []
# Face: colour effects and the mouth slide (its own WPO; skipped if the face already has one).
face = lib.load_asset(FACE)
call, status = wire_color(face, face_fx, "Color", "FaceColor", 300, -200)
wpo = mel.get_material_property_input_node(face, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
mouth_status = "skipped: face has its own world position offset"
if call is not None and wpo is None:
    assert mel.connect_material_property(call, "MouthOffset", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    mouth_status = "wired"
elif already_wired(wpo, face_fx):
    mouth_status = "already wired"
face.set_editor_property("used_with_skeletal_mesh", True)
face.set_editor_property("used_with_morph_targets", True)
mel.recompile_material(face)
assert lib.save_loaded_asset(face, only_if_is_dirty=False)
report.append({"material": FACE, "color": status, "mouth_slide": mouth_status})

# Body skin: flush, sweat beads, goosebumps after the existing toon/press chain.
body = lib.load_asset(BODY)
_, status = wire_color(body, face_fx, "Color", "BodyColor", 600, -200)
mel.recompile_material(body)
assert lib.save_loaded_asset(body, only_if_is_dirty=False)
report.append({"material": BODY, "color": status})

# Eyes: the iris texture is sampled through the dilated UV; hearts go over the result.
eyes = lib.load_asset(EYES)
samples = [n for n in mel.get_material_expressions(eyes) if isinstance(n, unreal.MaterialExpressionTextureSample)
           and n.get_editor_property("texture") and "eyes_diff" in n.get_editor_property("texture").get_name()]
assert samples, (EYES, "eyes_diff texture sample not found")
call, status = wire_color(eyes, eye_fx, "Color", "Color", 300, -200)
if call is not None:
    # A second call feeds the sample's UVs (one call for both would loop sample -> call -> sample).
    uv_call = mel.create_material_expression(eyes, unreal.MaterialExpressionMaterialFunctionCall, -900, 200)
    uv_call.set_editor_property("material_function", eye_fx)
    for sample in samples:
        assert mel.connect_material_expressions(uv_call, "UV", sample, "UVs"), EYES
eyes.set_editor_property("used_with_skeletal_mesh", True)
eyes.set_editor_property("used_with_morph_targets", True)
mel.recompile_material(eyes)
assert lib.save_loaded_asset(eyes, only_if_is_dirty=False)
report.append({"material": EYES, "color": status, "pupil_uv_samples": len(samples)})

out = ROOT / "evidence/05/face_shader_setup.json"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps({"functions": [face_fx.get_path_name(), eye_fx.get_path_name()], "materials": report,
                           "parameters": {"scalars": SCALARS, "vectors": VECTORS}}, indent=2), encoding="utf-8")
unreal.log("GRATIA_FACE_SHADERS_PASS " + json.dumps(report))
