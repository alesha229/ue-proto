"""Preserve the existing PhysicsAsset and repair scaled FBX secondary shape dimensions."""
import json
import shutil
from pathlib import Path
import unreal

root = Path(__file__).resolve().parents[2]
lib = unreal.EditorAssetLibrary
mesh = lib.load_asset('/Game/Gratia/GameRig/SK_Gratia_Game')
physics = mesh.get_editor_property('physics_asset')
path = root / 'GratiaVR/Content/Gratia/Physics/PA_Gratia_MVP.uasset'
backup = root / 'evidence/04/PA_Gratia_before_shape_units.uasset'
if not backup.exists():
    shutil.copy2(path, backup)
assert unreal.GratiaPortLibrary.repair_secondary_shape_units(mesh)
assert lib.save_loaded_asset(physics, only_if_is_dirty=False)
unreal.log('GRATIA_SECONDARY_SHAPE_UNITS_REPAIRED')
