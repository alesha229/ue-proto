"""Author DA_Gratia.BodySurface: the touchable body for hands, fitted to visible skin and clothing.

Editor commandlet after the C++ editor build (UGratiaBodySurface uses it at runtime):
- limbs: capsules per bone (outer layer, two per long tapering part);
- torso: horizontal stadium slices from all torso vertices (belly, back, waist, chest), so a
  front-only bone (lower belly) is still a closed body; wrap axis along the spine;
- breasts/butt: spheres on the KawaiiPhysics bones (soft, move with the physics; grabbed by
  the soft-body grab, not the wrapping grip);
- head and neck: solid for the hands but not grip targets.
Intimate, finger and hanging-accessory bones are not part of the surface.
"""
import json
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[2]
lib = unreal.EditorAssetLibrary
setup = unreal.GratiaSoftBodySetupLibrary
mesh = lib.load_asset("/Game/Gratia/GameRig/SK_Gratia_Game")
profile = lib.load_asset("/Game/Characters/Profiles/DA_Gratia")
assert isinstance(mesh, unreal.SkeletalMesh) and isinstance(profile, unreal.GratiaCharacterProfile)

slots = [str(m.get_editor_property("imported_material_slot_name")) for m in mesh.get_editor_property("materials")]
include = [name for name in ("Body_skin", "Default cloth 1", "Default cloth 2") if name in slots]
head_include = include + [name for name in ("Gratia_face",) if name in slots]
assert include, slots

component = unreal.SkeletalMeshComponent()
component.set_skinned_asset_and_update(mesh)
bones = [str(component.get_bone_name(i)) for i in range(component.get_num_bones())]
soft = [str(b) for chain in profile.get_editor_property("soft_body").get_editor_property("chains")
        for b in chain.get_editor_property("root_bones")]
head = str(profile.resolve_bone("Head"))
neck = []
if head in bones:
    parent = str(component.get_parent_bone(head))
    while parent and parent != "None" and "spine" in parent.lower() and len(neck) < 4:
        neck.append(parent)
        if parent.lower().endswith(("spine_004", "spine.004")):
            break
        parent = str(component.get_parent_bone(parent))
torso = [b for b in bones if b not in neck and b != head and b not in soft
         and any(t in b.lower() for t in ("spine", "pelvis"))]
attach = [b for b in torso if "spine" in b.lower() and "tweak" not in b.lower()]
# Hanging accessories (tie, pads, decor), intimate and finger bones are not body surface.
skip_tokens = ("ero", "f_index", "f_middle", "f_ring", "f_pinky", "thumb", "palm", "tie", "pad", "decor", "toe")
excluded = sorted({b for b in bones if b in soft or b == head or b in neck or b in torso
                   or any(t in b.lower() for t in skip_tokens)})


def tagged(capsules, grip, soft_part):
    result = []
    for capsule in capsules or []:
        capsule.set_editor_property("grip", grip)
        capsule.set_editor_property("soft", soft_part)
        result.append(capsule)
    return result


# 80th percentile: the hand meets the outer layer (clothing, skirt panels), not the middle of it.
# Thigh jiggle bones share the thigh's flesh: their skin counts for the thigh capsules.
limb_soft = [b for b in soft if "thigh" in b.lower()]
limbs = tagged(setup.measure_limb_surface(mesh, include, excluded, limb_soft, 40, 0.8), True, False)
limbs = [c for c in limbs if c.get_editor_property("radius_cm") >= 1.0 or "hand" in str(c.get_editor_property("bone")).lower()]
# Slices use every vertex at their height except limbs, head/neck, soft parts and dangling
# pieces: the belly front and the clothing over it belong to many bones, and a ring made only
# of torso-bone vertices misses its front. Fine slices (3.5 cm) keep the merged surface smooth.
not_torso = ("arm", "hand", "shoulder", "thigh", "shin", "foot", "tail", "hair", "ear") + skip_tokens
slice_vertex_bones = [b for b in bones if b not in soft and b != head and b not in neck
                      and not any(t in b.lower() for t in not_torso)]
slices = tagged(setup.measure_torso_slices(mesh, include, slice_vertex_bones, attach, 3.5), True, False)
# Soft parts: median radius (through the skin, not around it); the dent shows the contact. Spheres
# follow the bones of the skin they fit (butt: soft bone, pelvis and thigh). Limb soft bones (thighs)
# lie inside their limb: the limb capsule is their surface, no sphere.
sphere_soft = [b for b in soft if "thigh" not in b.lower()]
softs = tagged(setup.measure_sphere_surface(mesh, include, sphere_soft, 0.5), False, True)
heads = tagged(setup.measure_sphere_surface(mesh, head_include, [head], 0.8), False, False)
necks = tagged(setup.measure_body_surface(mesh, head_include, [b for b in bones if b not in neck], 20, 0.8), False, False)
assert limbs and slices, "body surface fit failed"
assert len(softs) == len(sphere_soft), ("soft part spheres", len(softs), sphere_soft)
assert heads, "head sphere fit failed"

capsules = limbs + slices + softs + heads + necks
profile.set_editor_property("body_surface", capsules)
assert lib.save_loaded_asset(profile, only_if_is_dirty=False)


def row(c, kind):
    return {"bone": str(c.get_editor_property("bone")), "kind": kind, "radius_cm": round(c.get_editor_property("radius_cm"), 2),
            "grip": c.get_editor_property("grip"), "soft": c.get_editor_property("soft")}


report = ([row(c, "limb") for c in limbs] + [row(c, "torso") for c in slices] + [row(c, "soft") for c in softs]
          + [row(c, "head") for c in heads] + [row(c, "neck") for c in necks])
(ROOT / "evidence/05/body_surface_setup.json").write_text(
    json.dumps({"include_slots": include, "torso_bones": torso, "attach_bones": attach, "neck": neck, "head": head,
                "excluded_limb_bones": excluded, "capsules": report}, indent=2), encoding="utf-8")
unreal.log("GRATIA_BODY_SURFACE_PASS capsules=%d limbs=%d torso=%d soft=%d head=%d neck=%d" % (
    len(capsules), len(limbs), len(slices), len(softs), len(heads), len(necks)))
