"""The player's arms: a skinned forearm and hand per side, cut out of the UE5 template Manny body, and a skin material.

Editor commandlet, run after the C++ editor build:
  UnrealEditor-Cmd GratiaVR.uproject -run=pythonscript -script=Scripts/setup_player_arms.py -unattended -NullRHI
Creates in /Game/Gratia/PlayerArms
- SKM_PlayerArm_L / SKM_PlayerArm_R: copies of /Game/Characters/Mannequins/Meshes/SKM_Manny_Simple (UE5 template content,
  Epic Content License: usable in Unreal Engine projects) keeping only the triangles skinned to lowerarm_* and everything
  below it (forearm, hand, fingers). AGratiaStage1Runtime poses them from the animated XR hand
  (finger curl and contact, grip) and the solved elbow; the XR hand keeps its collision proxies.
- M_PlayerSkin / MI_PlayerSkin: unlit toon skin like the character's (tone x (0.72 ambient + 0.28 key light), a warm rim).
Overwrites what it created before.
"""
import unreal

lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
BASE = '/Game/Gratia/PlayerArms'
SOURCE = '/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'

if not lib.does_directory_exist(BASE):
    lib.make_directory(BASE)

# ------------------------------------------------------------------ material
MAT = BASE + '/M_PlayerSkin'
if lib.does_asset_exist(MAT):
    lib.delete_asset(MAT)
material = tools.create_asset('M_PlayerSkin', BASE, unreal.Material, unreal.MaterialFactoryNew())
material.set_editor_property('two_sided', False)


def param(kind, name, x, y, value):
    node = mel.create_material_expression(material, kind, x, y)
    node.set_editor_property('parameter_name', name)
    if kind == unreal.MaterialExpressionVectorParameter:
        node.set_editor_property('default_value', unreal.LinearColor(*value))
    else:
        node.set_editor_property('default_value', value)
    return node


tone = param(unreal.MaterialExpressionVectorParameter, 'SkinTone', -900, -200, (0.93, 0.74, 0.64, 1.0))
rim_tone = param(unreal.MaterialExpressionVectorParameter, 'RimTone', -900, 0, (1.0, 0.62, 0.55, 1.0))
rim_amount = param(unreal.MaterialExpressionScalarParameter, 'RimAmount', -900, 150, 0.3)
# The character's toon light (port_gratia_toon_normals.py): 0.72 ambient + 0.28 x the key light from the same direction,
# so the player's arms sit in the same unlit look as the character instead of a darker lit surface.
normal = mel.create_material_expression(material, unreal.MaterialExpressionVertexNormalWS, -900, 300)
key = mel.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -900, 380)
key.set_editor_property('constant', unreal.LinearColor(-.55, -.2, .811, 1))
dot = mel.create_material_expression(material, unreal.MaterialExpressionDotProduct, -700, 320)
mel.connect_material_expressions(normal, '', dot, 'A')
mel.connect_material_expressions(key, '', dot, 'B')
clamp = mel.create_material_expression(material, unreal.MaterialExpressionSaturate, -550, 320)
mel.connect_material_expressions(dot, '', clamp, '')
gain = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -420, 320)
gain.set_editor_property('const_b', 0.28)
mel.connect_material_expressions(clamp, '', gain, 'A')
ambient = mel.create_material_expression(material, unreal.MaterialExpressionAdd, -300, 320)
ambient.set_editor_property('const_b', 0.72)
mel.connect_material_expressions(gain, '', ambient, 'A')
fresnel = mel.create_material_expression(material, unreal.MaterialExpressionFresnel, -650, 100)
fresnel.set_editor_property('exponent', 3.0)
rim = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -450, 100)
mel.connect_material_expressions(fresnel, '', rim, 'A')
mel.connect_material_expressions(rim_amount, '', rim, 'B')
color = mel.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -250, -100)
mel.connect_material_expressions(tone, '', color, 'A')
mel.connect_material_expressions(rim_tone, '', color, 'B')
mel.connect_material_expressions(rim, '', color, 'Alpha')
lit = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -100, 0)
mel.connect_material_expressions(color, '', lit, 'A')
mel.connect_material_expressions(ambient, '', lit, 'B')
material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
material.set_editor_property('used_with_skeletal_mesh', True)
mel.connect_material_property(lit, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mel.recompile_material(material)
lib.save_asset(MAT)
MI = BASE + '/MI_PlayerSkin'
if lib.does_asset_exist(MI):
    lib.delete_asset(MI)
instance = tools.create_asset('MI_PlayerSkin', BASE, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
mel.set_material_instance_parent(instance, material)
lib.save_asset(MI)

# ------------------------------------------------------------------ meshes
for side in ('L', 'R'):
    path = f'{BASE}/SKM_PlayerArm_{side}'
    if lib.does_asset_exist(path):
        lib.delete_asset(path)
    mesh = lib.duplicate_asset(SOURCE, path)
    assert mesh, f'could not copy {SOURCE}'
    kept = unreal.GratiaExperienceToolsLibrary.keep_triangles_on_bones(mesh, [f'lowerarm_{side.lower()}'], True, 0.5)
    assert kept > 500, f'{path}: only {kept} triangles kept'
    lib.save_asset(path)
    unreal.log_warning(f'PLAYER_ARMS {path} triangles={kept}')
unreal.log_warning('PLAYER_ARMS done')
