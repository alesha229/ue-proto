"""Import the MCP-validated Zenless-Zone-Zero-style clips (author_anime_clips.py) and route them.

Editor commandlet after their FBX export. Only animations are imported: the mesh already carries
their Game_AnimeRef_* corrective morphs. Routing in DA_Gratia:
- ReactStartle: StrongReactionClip (fast touch, any zone);
- ReactHappy / ReactShy: MoodReactionClips Cheerful / Reserved;
- ReactPout: zone Hair;
- IdleAnime: performance clip "Idle ZZZ" (looping, Pose menu / F2).
"""
import hashlib
import json
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[2]
REPORT_PATH = ROOT / "evidence/05/blender_mcp/anime_clips_validation.json"
OUT = ROOT / "evidence/05/blender_mcp"
report = json.loads(REPORT_PATH.read_text(encoding="utf-8"))
clips = {c["clip"]: c for c in report["clips"]}
wanted = ["IdleAnime", "ReactShy", "ReactHappy", "ReactPout", "ReactStartle"]
for name in wanted:
    clip = clips[name]
    assert clip.get("exported") and (clip["passed"] or clip.get("accepted_exception")), name
    fbx = Path(clip["fbx"])
    assert fbx.is_file() and fbx.stat().st_size == clip["bytes"], str(fbx)
    assert hashlib.sha256(fbx.read_bytes()).hexdigest() == clip["sha256"], "FBX differs from the validated export: " + name

lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
directory = "/Game/Gratia/GameRig"
mesh = lib.load_asset(directory + "/SK_Gratia_Game")
profile = lib.load_asset("/Game/Characters/Profiles/DA_Gratia")
assert isinstance(mesh, unreal.SkeletalMesh) and isinstance(profile, unreal.GratiaCharacterProfile)
skeleton = mesh.get_editor_property("skeleton")
morphs = {m.get_name().lower() for m in mesh.get_editor_property("morph_targets")}
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, "Interchange.FeatureFlags.Import.FBX 0")

imported, curves = {}, {}
for name in wanted:
    clip = clips[name]
    asset_name = "A_Gratia_Game_" + name
    options = unreal.FbxImportUI()
    for key, value in dict(import_mesh=False, import_animations=True, import_materials=False,
                           import_textures=False, automated_import_should_detect_type=False,
                           skeleton=skeleton, mesh_type_to_import=unreal.FBXImportType.FBXIT_ANIMATION,
                           original_import_type=unreal.FBXImportType.FBXIT_ANIMATION,
                           override_animation_name=asset_name).items():
        options.set_editor_property(key, value)
    data = options.get_editor_property("anim_sequence_import_data")
    for key, value in dict(convert_scene=True, convert_scene_unit=True, use_default_sample_rate=False,
                           import_custom_attribute=False,
                           animation_length=unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME).items():
        data.set_editor_property(key, value)
    task = unreal.AssetImportTask()
    for key, value in dict(filename=clip["fbx"], destination_path=directory, destination_name=asset_name, automated=True,
                           replace_existing=True, replace_existing_settings=True, save=True,
                           factory=unreal.FbxFactory(), options=options).items():
        task.set_editor_property(key, value)
    tools.import_asset_tasks([task])
    anim = next(a for a in (lib.load_asset(p) for p in task.get_editor_property("imported_object_paths")) if isinstance(a, unreal.AnimSequence))
    assert anim.get_editor_property("skeleton") == skeleton, name
    assert abs(anim.get_play_length() - clip["duration_seconds"]) < 0.04, (name, anim.get_play_length())
    ranges = {}
    for curve_name in unreal.AnimationLibrary.get_animation_curve_names(anim, unreal.RawCurveTrackTypes.RCT_FLOAT):
        times, values = unreal.AnimationLibrary.get_float_keys(anim, curve_name)
        if values:
            ranges[str(curve_name).lower()] = {"min": min(values), "max": max(values)}  # FName case is not stable
    prefix = ("Game_AnimeRef_%s_" % name).lower()
    correctives = {k: v for k, v in ranges.items() if k.startswith(prefix)}
    assert correctives and all(k in morphs for k in correctives), (name, "corrective morphs missing", sorted(correctives))
    corrective_peak = max(max(abs(v["max"]), abs(v["min"])) for v in correctives.values())
    assert corrective_peak > 1e-5, (name, "correctives are flat")
    imported[name] = anim
    curves[name] = {"length": anim.get_play_length(), "correctives": len(correctives), "corrective_peak": corrective_peak,
                    "face_peak": max((max(abs(v["min"]), abs(v["max"])) for k, v in ranges.items() if not k.startswith("game_")), default=0.0)}

profile.set_editor_property("strong_reaction_clip", imported["ReactStartle"])
profile.set_editor_property("mood_reaction_clips", {"Cheerful": imported["ReactHappy"], "Reserved": imported["ReactShy"]})
routing = dict(profile.get_editor_property("reaction_clips"))
routing["Hair"] = imported["ReactPout"]
profile.set_editor_property("reaction_clips", routing)
entry = unreal.GratiaPerformanceClip()
entry.set_editor_property("name", "Idle ZZZ")
entry.set_editor_property("clip", imported["IdleAnime"])
entry.set_editor_property("loop", True)
performances = [c for c in profile.get_editor_property("performance_clips") if str(c.get_editor_property("name")) != "Idle ZZZ"]
profile.set_editor_property("performance_clips", [entry] + performances)
validation = profile.validate_profile()
errors = validation[0] if len(validation) == 2 else validation[1]
assert not errors, list(errors)
assert lib.save_loaded_asset(profile, only_if_is_dirty=False)
output = {"clips": curves, "routing": {"strong": "ReactStartle", "Cheerful": "ReactHappy", "Reserved": "ReactShy", "Hair": "ReactPout",
                                        "performance": "Idle ZZZ"},
          "validation_sha256": hashlib.sha256(REPORT_PATH.read_bytes()).hexdigest()}
(OUT / "anime_clips_unreal_import.json").write_text(json.dumps(output, indent=2), encoding="utf-8")
unreal.log("GRATIA_ANIME_CLIPS_IMPORTED " + json.dumps(curves))
