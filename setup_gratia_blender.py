import bpy
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "Gratia.blend"
WORKING = ROOT / "Gratia_working.blend"


def ensure_collection(name):
    collection = bpy.data.collections.get(name)
    if collection is None:
        collection = bpy.data.collections.new(name)
        bpy.context.scene.collection.children.link(collection)
    return collection


if not SOURCE.exists():
    raise FileNotFoundError(SOURCE)

# This script is run against the source file and saves a separate working copy.
bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
scene = bpy.context.scene

scene.unit_settings.system = "METRIC"
scene.unit_settings.length_unit = "METERS"
scene.unit_settings.scale_length = 1.0
scene.render.fps = 30
scene.render.fps_base = 1.0

# Keep viewport colors predictable while evaluating materials.
scene.view_settings.look = "Medium High Contrast"

for name in (
    "GRATIA_CHARACTER",
    "GRATIA_BODY",
    "GRATIA_CLOTHING",
    "GRATIA_HAIR",
    "GRATIA_FACE",
    "GRATIA_PHYSICS",
    "GRATIA_EXPORT",
):
    ensure_collection(name)

scene["project_name"] = "Gratia VR MVP"
scene["target_engine"] = "Unreal Engine"
scene["target_platform"] = "PC VR"
scene["source_file"] = SOURCE.name
scene["working_file"] = WORKING.name
scene["export_notes"] = "Do not edit the source file. Prepare export copies only."

# Make a small, explicit checklist visible in the scene custom properties.
scene["audit_01_scale"] = "pending"
scene["audit_02_armature"] = "pending"
scene["audit_03_materials"] = "pending"
scene["audit_04_face_shapes"] = "pending"
scene["audit_05_clothing_physics"] = "pending"
scene["audit_06_export_test"] = "pending"

bpy.ops.wm.save_as_mainfile(filepath=str(WORKING))
print(f"Created working file: {WORKING}")
