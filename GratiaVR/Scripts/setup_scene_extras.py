"""Assets for the in-scene extras (mirror, customization) of the body-motion/ambience layer.

Editor commandlet, run after the C++ editor build:
  UnrealEditor-Cmd.exe GratiaVR.uproject -run=pythonscript -script="<this file>"
1. Creates /Game/Gratia/Environment/M_GratiaMirror (white metal, roughness 0.02): the glass of AGratiaMirror.
   Without it packaged builds show the default material (the editor makes a transient one).
2. Fills DA_Gratia.Customization when it is empty: one slot per clothing layer of SK_Gratia_Game
   (material slots whose name contains "cloth"), options "Надето" / "Снято". Existing slots are kept.
   Hairstyles/accessories need their own meshes; add them as options in the profile when they exist.
Report: evidence/05/scene_extras_setup.json.
"""
import json
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[2]
lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
report = {}

MIRROR = "/Game/Gratia/Environment/M_GratiaMirror"
if lib.does_asset_exist(MIRROR):
    report["mirror_material"] = "exists"
else:
    material = tools.create_asset("M_GratiaMirror", "/Game/Gratia/Environment", unreal.Material, unreal.MaterialFactoryNew())
    color = mel.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -300, 0)
    color.set_editor_property("constant", unreal.LinearColor(0.93, 0.93, 0.95, 1.0))
    metallic = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -300, 150)
    metallic.set_editor_property("r", 1.0)
    roughness = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -300, 250)
    roughness.set_editor_property("r", 0.02)
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(metallic, "", unreal.MaterialProperty.MP_METALLIC)
    mel.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(material)
    lib.save_asset(MIRROR)
    report["mirror_material"] = "created"

PROFILE = "/Game/Characters/Profiles/DA_Gratia"
profile = lib.load_asset(PROFILE)
slots = list(profile.get_editor_property("customization"))
if slots:
    report["customization"] = f"kept {len(slots)} existing slots"
else:
    mesh = profile.get_editor_property("mesh")
    names = [str(m.get_editor_property("material_slot_name")) for m in mesh.get_editor_property("materials")]
    cloth = [n for n in names if "cloth" in n.lower()]
    for index, name in enumerate(cloth):
        worn = unreal.GratiaCustomizationOption()
        worn.set_editor_property("label", "Надето")
        off = unreal.GratiaCustomizationOption()
        off.set_editor_property("label", "Снято")
        off.set_editor_property("hidden_material_slots", [name])
        slot = unreal.GratiaCustomizationSlot()
        slot.set_editor_property("name", name)
        slot.set_editor_property("label", f"Одежда {index + 1}")
        slot.set_editor_property("options", [worn, off])
        slots.append(slot)
    profile.set_editor_property("customization", slots)
    lib.save_asset(PROFILE)
    report["customization"] = {"material_slots": names, "clothing_slots": cloth}

out = ROOT / "evidence" / "05" / "scene_extras_setup.json"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
unreal.log(f"Scene extras setup: {report}")
