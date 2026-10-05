"""Author DA_Gratia.BodySurface: capsules fitted to the visible skin and clothing per bone.

Editor commandlet after the C++ editor build. Hands collide with these capsules by the palm,
lean onto them and wrap around them on grip (UGratiaBodySurface). Soft parts (breasts, butt)
stay KawaiiPhysics zones; head/neck, intimate and finger bones are not grip targets.
"""
import json
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[2]
lib = unreal.EditorAssetLibrary
mesh = lib.load_asset("/Game/Gratia/GameRig/SK_Gratia_Game")
profile = lib.load_asset("/Game/Characters/Profiles/DA_Gratia")
assert isinstance(mesh, unreal.SkeletalMesh) and isinstance(profile, unreal.GratiaCharacterProfile)

slots = [str(m.get_editor_property("imported_material_slot_name")) for m in mesh.get_editor_property("materials")]
include = [name for name in ("Body_skin", "Default cloth 1", "Default cloth 2") if name in slots]
assert include, slots

component = unreal.SkeletalMeshComponent()
component.set_skinned_asset_and_update(mesh)
bones = [str(component.get_bone_name(i)) for i in range(component.get_num_bones())]
soft = [str(b) for chain in profile.get_editor_property("soft_body").get_editor_property("chains")
        for b in chain.get_editor_property("root_bones")]
head = str(profile.resolve_bone("Head"))
# Hanging accessories (tie, pads, decor) are not solid body parts for the hands.
exclude_tokens = ("ero", "f_index", "f_middle", "f_ring", "f_pinky", "thumb", "palm", "breast", "ass",
                  "tie", "pad", "decor", "toe")
exclude = sorted({b for b in bones if b in soft or b == head or any(t in b.lower() for t in exclude_tokens)})
# Neck: bones between the chest and the head on the spine chain.
if head and head in bones:
    parent = str(component.get_parent_bone(head))
    while parent and parent != "None" and "spine" in parent.lower() and len(exclude) < len(bones):
        exclude.append(parent)
        if parent.lower().endswith(("spine_004", "spine.004")):
            break
        parent = str(component.get_parent_bone(parent))

# 80th percentile: the hand meets the outer layer (clothing, skirt panels), not the middle of it.
capsules = unreal.GratiaSoftBodySetupLibrary.measure_body_surface(mesh, include, exclude, 40, 0.8)
assert capsules, "no body surface capsules"
# Degenerate fits (under 1 cm, except the thin hands) are not usable surfaces.
capsules = [c for c in capsules if c.get_editor_property("radius_cm") >= 1.0 or "hand" in str(c.get_editor_property("bone")).lower()]
profile.set_editor_property("body_surface", capsules)
assert lib.save_loaded_asset(profile, only_if_is_dirty=False)
report = [{"bone": str(c.get_editor_property("bone")), "radius_cm": round(c.get_editor_property("radius_cm"), 2)} for c in capsules]
(ROOT / "evidence/05/body_surface_setup.json").write_text(
    json.dumps({"include_slots": include, "excluded_bones": exclude, "capsules": report}, indent=2), encoding="utf-8")
unreal.log("GRATIA_BODY_SURFACE_PASS capsules=%d %s" % (len(capsules), json.dumps(report)))
