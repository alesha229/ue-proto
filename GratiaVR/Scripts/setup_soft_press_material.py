"""Surface press (squeeze) of skin and clothing: MPC + material function + material wiring.

Editor commandlet, run after the C++ editor build. Creates
- /Game/Gratia/Physics/MPC_GratiaSoftPress: Sphere00..23 (hand palm/finger spheres, world cm),
  Zone0..3 (soft zone masks), Config (softness cm, -, zone falloff cm, strength);
- /Game/Gratia/CharacterMaterials/MF_GratiaSoftPress: vertex offset (dent with a smooth rim)
  plus a bent lighting normal and slight darkening inside the dent;
and wires it into the skin and cloth materials (Unlit toon: emissive = diffuse * light term).
Skin uses a layer offset so it always stays under the pressed clothing.
The C++ runtime (UGratiaSoftBodyInteraction) writes the collection every tick.
"""
import json
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[2]
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
SPHERES = [f"Sphere{i:02d}" for i in range(24)]
ZONES = [f"Zone{i}" for i in range(4)]
# Skin is pushed 0.35 cm further than the clothing above it.
TARGETS = {
    "/Game/Gratia/CharacterMaterials/M_Gratia_Body_skin": 0.35,
    "/Game/Gratia/CharacterMaterials/M_Gratia_Default_cloth_1": 0.0,
    "/Game/Gratia/CharacterMaterials/M_Gratia_Default_cloth_2": 0.0,
}

# ------------------------------------------------------------------ collection
MPC_PATH = "/Game/Gratia/Physics/MPC_GratiaSoftPress"
if lib.does_asset_exist(MPC_PATH):
    collection = lib.load_asset(MPC_PATH)
else:
    collection = tools.create_asset("MPC_GratiaSoftPress", "/Game/Gratia/Physics", unreal.MaterialParameterCollection,
                                     unreal.MaterialParameterCollectionFactoryNew())
existing = {str(p.get_editor_property("parameter_name")) for p in collection.get_editor_property("vector_parameters")}
vectors = list(collection.get_editor_property("vector_parameters"))
for name, default in [(n, unreal.LinearColor(0, 0, -100000, 0)) for n in SPHERES + ZONES] + [("Config", unreal.LinearColor(1.5, 1, 4, 0))]:
    if name in existing:
        continue
    parameter = unreal.CollectionVectorParameter()
    parameter.set_editor_property("parameter_name", name)
    parameter.set_editor_property("default_value", default)
    vectors.append(parameter)
collection.set_editor_property("vector_parameters", vectors)
assert lib.save_loaded_asset(collection, only_if_is_dirty=False)

# ------------------------------------------------------------------ HLSL
ZONE_MASK = """
float4 Zs[4] = {Z0, Z1, Z2, Z3};
float Mask = 0;
[unroll] for (int z = 0; z < 4; ++z)
{
    if (Zs[z].w > 0)
        Mask = max(Mask, saturate(1 - (length(Pos - Zs[z].xyz) - Zs[z].w) / max(Cfg.z, 0.01)));
}
"""
SPHERE_ARRAY = "float4 Ss[24] = {" + ", ".join(f"S{i}" for i in range(24)) + "};\n"
WPO_CODE = ("float3 Pos = P;\n" + ZONE_MASK + """
if (Mask <= 0 || Cfg.w <= 0) return float3(0, 0, 0);
""" + SPHERE_ARRAY + """
float Soft = max(Cfg.x, 0.05);
float3 Offset = 0;
for (int i = 0; i < 24; ++i)
{
    if (Ss[i].w <= 0) continue;
    float R = Ss[i].w + Layer;
    float3 V = Pos + Offset - Ss[i].xyz;
    float r = length(V);
    if (r >= R + Soft || r < 0.001) continue;
    // Smooth max(r, R): points inside the sphere move to its surface, the rim blends out over Soft.
    float x = clamp(r - R + Soft, 0, 2 * Soft);
    Offset += V / r * (R + x * x / (4 * Soft) - r);
}
return Offset * Mask * Cfg.w;
""")
PIXEL_CODE = ("float3 Pos = P;\n" + ZONE_MASK + """
float3 Normal = normalize(N);
if (Mask <= 0 || Cfg.w <= 0) return float4(Normal, 1);
""" + SPHERE_ARRAY + """
float Soft = max(Cfg.x, 0.05);
float3 Bend = 0;
float Weight = 0;
for (int i = 0; i < 24; ++i)
{
    if (Ss[i].w <= 0) continue;
    float3 V = Pos - Ss[i].xyz;
    float r = length(V);
    float w = saturate((Ss[i].w + Layer + 2 * Soft - r) / (2 * Soft));
    if (w > 0 && r > 0.001) { Bend += V / r * w; Weight = max(Weight, w); }
}
Weight *= Mask * Cfg.w;
if (Weight <= 0) return float4(Normal, 1);
// Dent walls face away from the pressing palm/finger; the bottom is slightly shadowed.
return float4(normalize(lerp(Normal, normalize(Bend), 0.85 * Weight)), 1 - 0.28 * Weight);
""")

# ------------------------------------------------------------------ material function
MF_PATH = "/Game/Gratia/CharacterMaterials/MF_GratiaSoftPress"
if lib.does_asset_exist(MF_PATH):
    function = lib.load_asset(MF_PATH)
    mel.delete_all_material_expressions_in_function(function)
else:
    function = tools.create_asset("MF_GratiaSoftPress", "/Game/Gratia/CharacterMaterials", unreal.MaterialFunction,
                                  unreal.MaterialFunctionFactoryNew())
function.set_editor_property("description", "Gratia soft press: dent of skin/clothing under hand spheres (MPC_GratiaSoftPress).")
expr = lambda cls, x, y: mel.create_material_expression_in_function(function, cls, x, y)

normal_in = expr(unreal.MaterialExpressionFunctionInput, -1400, -200)
normal_in.set_editor_property("input_name", "BaseNormal")
normal_in.set_editor_property("input_type", unreal.FunctionInputType.FUNCTION_INPUT_VECTOR3)
normal_in.set_editor_property("sort_priority", 0)
layer_in = expr(unreal.MaterialExpressionFunctionInput, -1400, 0)
layer_in.set_editor_property("input_name", "LayerOffsetCm")
layer_in.set_editor_property("input_type", unreal.FunctionInputType.FUNCTION_INPUT_SCALAR)
layer_in.set_editor_property("sort_priority", 1)
layer_in.set_editor_property("use_preview_value_as_default", True)

world_plain = expr(unreal.MaterialExpressionWorldPosition, -1400, 200)
world_plain.set_editor_property("world_position_shader_offset", unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
world_pixel = expr(unreal.MaterialExpressionWorldPosition, -1400, 300)

params = {}
for index, name in enumerate(SPHERES + ZONES + ["Config"]):
    node = expr(unreal.MaterialExpressionCollectionParameter, -1100, -600 + index * 60)
    node.set_editor_property("collection", collection)
    node.set_editor_property("parameter_name", name)
    params[name] = node


def custom(code, output_type, x, y, extra):
    node = expr(unreal.MaterialExpressionCustom, x, y)
    names = extra + [f"S{i}" for i in range(24)] + [f"Z{i}" for i in range(4)] + ["Cfg", "Layer"]
    inputs = []
    for name in names:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", name)
        inputs.append(item)
    node.set_editor_property("inputs", inputs)
    node.set_editor_property("code", code)
    node.set_editor_property("output_type", output_type)
    for i in range(24):
        assert mel.connect_material_expressions(params[SPHERES[i]], "", node, f"S{i}")
    for i in range(4):
        assert mel.connect_material_expressions(params[ZONES[i]], "", node, f"Z{i}")
    assert mel.connect_material_expressions(params["Config"], "", node, "Cfg")
    assert mel.connect_material_expressions(layer_in, "", node, "Layer")
    return node


wpo = custom(WPO_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, -600, -200, ["P"])
wpo.set_editor_property("description", "GratiaSoftPressOffset")
assert mel.connect_material_expressions(world_plain, "", wpo, "P")
shade = custom(PIXEL_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT4, -600, 200, ["P", "N"])
shade.set_editor_property("description", "GratiaSoftPressShade")
assert mel.connect_material_expressions(world_pixel, "", shade, "P")
assert mel.connect_material_expressions(normal_in, "", shade, "N")
normal_mask = expr(unreal.MaterialExpressionComponentMask, -300, 150)
for channel, value in (("r", True), ("g", True), ("b", True), ("a", False)):
    normal_mask.set_editor_property(channel, value)
shade_mask = expr(unreal.MaterialExpressionComponentMask, -300, 300)
for channel, value in (("r", False), ("g", False), ("b", False), ("a", True)):
    shade_mask.set_editor_property(channel, value)
assert mel.connect_material_expressions(shade, "", normal_mask, "")
assert mel.connect_material_expressions(shade, "", shade_mask, "")
outputs = {}
for index, (name, source) in enumerate((("WPO", wpo), ("Normal", normal_mask), ("Shade", shade_mask))):
    out = expr(unreal.MaterialExpressionFunctionOutput, 0, -200 + index * 200)
    out.set_editor_property("output_name", name)
    out.set_editor_property("sort_priority", index)
    assert mel.connect_material_expressions(source, "", out, "")
    outputs[name] = out
mel.update_material_function(function)
assert lib.save_loaded_asset(function, only_if_is_dirty=False)


# ------------------------------------------------------------------ materials
def find(material, node, cls, depth=0):
    if node is None or depth > 12:
        return None
    if isinstance(node, cls):
        return node
    for child in mel.get_inputs_for_material_expression(material, node):
        found = find(material, child, cls, depth + 1)
        if found:
            return found
    return None


report = []
for path, layer in TARGETS.items():
    material = lib.load_asset(path)
    current_wpo = mel.get_material_property_input_node(material, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    if isinstance(current_wpo, unreal.MaterialExpressionMaterialFunctionCall) and current_wpo.get_editor_property("material_function") == function:
        report.append({"material": path, "status": "already wired"})
        mel.recompile_material(material)
        assert lib.save_loaded_asset(material, only_if_is_dirty=False)
        continue
    assert current_wpo is None, (path, "has its own world position offset")
    emissive = mel.get_material_property_input_node(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    dot = find(material, emissive, unreal.MaterialExpressionDotProduct)
    assert dot, (path, "toon light term not found")
    base_normal = mel.get_inputs_for_material_expression(material, dot)[0]
    assert base_normal is not None, path
    call = mel.create_material_expression(material, unreal.MaterialExpressionMaterialFunctionCall, -900, 500)
    call.set_editor_property("material_function", function)
    layer_node = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -1150, 600)
    layer_node.set_editor_property("r", layer)
    assert mel.connect_material_expressions(layer_node, "", call, "LayerOffsetCm")
    assert mel.connect_material_expressions(base_normal, "", call, "BaseNormal")
    assert mel.connect_material_expressions(call, "Normal", dot, "A")
    multiply = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -250, 0)
    assert mel.connect_material_expressions(emissive, "", multiply, "A")
    assert mel.connect_material_expressions(call, "Shade", multiply, "B")
    assert mel.connect_material_property(multiply, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    assert mel.connect_material_property(call, "WPO", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    mel.recompile_material(material)
    assert lib.save_loaded_asset(material, only_if_is_dirty=False)
    report.append({"material": path, "status": "wired", "layer_offset_cm": layer})

profile = lib.load_asset("/Game/Characters/Profiles/DA_Gratia")
settings = profile.get_editor_property("soft_body")
settings.set_editor_property("press_collection", collection)
profile.set_editor_property("soft_body", settings)
assert lib.save_loaded_asset(profile, only_if_is_dirty=False)
out = ROOT / "evidence/05/soft_press_material_setup.json"
out.write_text(json.dumps({"collection": MPC_PATH, "function": MF_PATH, "materials": report}, indent=2), encoding="utf-8")
unreal.log("GRATIA_SOFT_PRESS_MATERIAL_PASS " + json.dumps(report))
