"""Morph mapping for the procedural face (UGratiaProceduralFace) in DA_Gratia.

Editor commandlet, run after the C++ editor build. Adds the semantic morphs the face layer uses to
DA_Gratia.SemanticMorphs (existing keys are kept) when SK_Gratia_Game has the morph, and leaves the
profile's Face settings at their C++ defaults. Missing morphs only switch the matching feature off.
Report: evidence/05/procedural_face_setup.json.
"""
import json
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[2]
lib = unreal.EditorAssetLibrary
PROFILE = "/Game/Characters/Profiles/DA_Gratia"

# Semantic key -> Gratia morph (original_working_morph_names in Exports/Gratia/GameRig/game_rig_manifest.json).
# LipBite and MouthNervous are a best guess from the names; check them in the morph preview.
MORPHS = {
    "BrowsDown": "Brows down", "BrowsWorry": "Brows worry 1", "BrowsAngry": "Brows angry 1",
    "EyesShrink": "Eyes shrink", "EyesSmug": "Eyes smug",
    "WinkLeft": "Eye L wink", "WinkRight": "Eye R wink",
    "MouthCornerLeftUp": "Mouth L up", "MouthCornerRightUp": "Mouth R up",
    "MouthPuff": "Mouth puff out", "LipBite": "Mouth nervous 2", "MouthNervous": "Mouth nervous 1",
    "MouthPout": "Mouth kiss",
}

profile = lib.load_asset(PROFILE)
mesh = profile.get_editor_property("mesh")
available = {str(m.get_name()) for m in mesh.get_editor_property("morph_targets")}
mapping = dict(profile.get_editor_property("semantic_morphs"))
added, kept, missing = {}, {}, {}
for key, morph in MORPHS.items():
    existing = {str(k): str(v) for k, v in mapping.items()}
    if key in existing:
        kept[key] = existing[key]
    elif morph in available:
        mapping[unreal.Name(key)] = unreal.Name(morph)
        added[key] = morph
    else:
        missing[key] = morph
profile.set_editor_property("semantic_morphs", mapping)
# Bool UFUNCTIONs with out parameters return the outputs (None when the validation failed).
result = profile.validate_profile()
errors, warnings = result if result is not None else (["ValidateProfile failed; see the log"], [])
if errors:
    raise RuntimeError(f"DA_Gratia invalid after the face mapping: {list(errors)}")
assert lib.save_loaded_asset(profile, only_if_is_dirty=False)
report = {"profile": PROFILE, "added": added, "kept": kept, "missing_on_mesh": missing,
          "face_settings": "C++ defaults (FGratiaFaceSettings)", "warnings": [str(w) for w in warnings]}
out = ROOT / "evidence/05/procedural_face_setup.json"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
unreal.log("GRATIA_PROCEDURAL_FACE_PASS " + json.dumps(report, ensure_ascii=False))
