import bpy
from pathlib import Path


ROOT = Path(__file__).resolve().parent
TEXTURES = ROOT / "textures"
WORKING = ROOT / "Gratia_working.blend"

if not TEXTURES.exists():
    raise FileNotFoundError(TEXTURES)

bpy.ops.wm.open_mainfile(filepath=str(WORKING))

linked = []
missing = []
for image in bpy.data.images:
    if image.name == "Render Result" or image.source not in {"FILE", "MOVIE"}:
        continue

    filename = Path(image.filepath.replace("\\", "/")).name
    if not filename:
        filename = image.name
        if filename.endswith(".001"):
            filename = filename[:-4]
    candidate = TEXTURES / filename
    if candidate.is_file():
        image.filepath = str(candidate)
        image.reload()
        linked.append((image.name, image.size[:]))
    else:
        missing.append(filename)

# Embed source textures so the working file remains portable. Some legacy
# entries in the source file point at a directory; pack valid images only.
packed = 0
for image in bpy.data.images:
    if image.name == "Render Result" or image.source != "FILE":
        continue
    if image.packed_file:
        continue
    if image.filepath and Path(bpy.path.abspath(image.filepath)).is_file():
        try:
            image.pack()
            packed += 1
        except RuntimeError:
            missing.append(image.name)

bpy.ops.wm.save_as_mainfile(filepath=str(WORKING))

print("LINKED", len(linked))
print("MISSING", sorted(set(missing)))
print("SIZES", linked)
print("PACKED", packed)
