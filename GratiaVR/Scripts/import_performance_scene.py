"""Attach the KM466 scene (music, partner body and partner viewpoint) to DA_Gratia's KM466 Full performance.

Editor commandlet after the C++ editor build. Inputs:
- Exports/Gratia/Audio/KM466_Music.wav: ffmpeg decode of the package's `Custom/Sounds/music over anim.mp3`
  (44.1 kHz stereo PCM); imported as /Game/Gratia/Performance/S_KM466_Music.
- evidence/05/kitty_mocap_full/km466_scene_partner.json from extract_vam_scene_partner.py.
The partner is the Epic mannequin (SKM_Manny_Simple) posed like the VaM partner atom.
License: the KM466 package (incl. its music) is CC BY-NC-ND - personal use only, do not distribute.
"""
import json
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[2]
WAV = ROOT / "Exports/Gratia/Audio/KM466_Music.wav"
DATA = json.loads((ROOT / "evidence/05/kitty_mocap_full/km466_scene_partner.json").read_text(encoding="utf-8"))
PERFORMANCES = ("KM466 Full",)
lib = unreal.EditorAssetLibrary
assert WAV.is_file(), WAV

task = unreal.AssetImportTask()
for key, value in dict(filename=str(WAV), destination_path="/Game/Gratia/Performance", destination_name="S_KM466_Music",
                       automated=True, replace_existing=True, save=True).items():
    task.set_editor_property(key, value)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
music = lib.load_asset("/Game/Gratia/Performance/S_KM466_Music")
assert isinstance(music, unreal.SoundWave), "music import failed"
assert abs(music.get_editor_property("duration") - 568.3) < 1.0, music.get_editor_property("duration")
partner = lib.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")
assert isinstance(partner, unreal.SkeletalMesh), "Epic mannequin mesh missing"


def vector(values):
    return unreal.Vector(*values)


aims = []
for item in DATA["aims"]:
    aim = unreal.GratiaPartnerAim()
    for key, value in (("bone", unreal.Name(item["bone"])), ("child", unreal.Name(item["child"])), ("From", vector(item["from"])),
                       ("To", vector(item["to"])), ("place", item["place"])):
        aim.set_editor_property(key, value)
    aims.append(aim)
pt = DATA["partner_transform"]
partner_transform = unreal.Transform(vector(pt["location_cm"]), unreal.Quat(*pt["quat_xyzw"]).rotator(), unreal.Vector(1, 1, 1))
vp = DATA["viewpoint"]
view_rotation = unreal.MathLibrary.make_rot_from_xz(vector(vp["x_axis"]), vector(vp["z_axis"]))
viewpoint = unreal.Transform(vector(vp["location_cm"]), view_rotation, unreal.Vector(1, 1, 1))

profile = lib.load_asset("/Game/Characters/Profiles/DA_Gratia")
clips = list(profile.get_editor_property("performance_clips"))
updated = []
for entry in clips:
    if str(entry.get_editor_property("name")) not in PERFORMANCES:
        continue
    scene = entry.get_editor_property("scene")
    for key, value in dict(music=music, music_volume=0.8, partner_mesh=partner, partner_transform=partner_transform,
                           partner_pose=aims, partner_hidden_in_viewpoint=[unreal.Name("head")], viewpoint=viewpoint).items():
        scene.set_editor_property(key, value)
    scene.set_editor_property("viewpoint", viewpoint)
    scene.set_editor_property("has_viewpoint", True)
    entry.set_editor_property("scene", scene)
    updated.append(str(entry.get_editor_property("name")))
assert updated, "no KM466 performance in DA_Gratia"
profile.set_editor_property("performance_clips", clips)
validation = profile.validate_profile()
errors = validation[0] if len(validation) == 2 else validation[1]
assert not errors, list(errors)
assert lib.save_loaded_asset(profile, only_if_is_dirty=False)
result = {"music": music.get_path_name(), "music_seconds": music.get_editor_property("duration"), "partner": partner.get_path_name(),
          "aims": len(aims), "performances": updated}
(ROOT / "evidence/05/kitty_mocap_full/km466_scene_import.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
unreal.log("GRATIA_PERFORMANCE_SCENE_IMPORTED " + json.dumps(result))
