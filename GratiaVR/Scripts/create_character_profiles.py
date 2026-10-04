"""Create editor-editable profiles. Run with Unreal's Python commandlet after C++ compilation.

This only creates/updates profile assets. It never changes a character mesh, skeleton,
material or map. A missing second full-body model can be seeded from the installed
Epic template resources without overwriting any existing project asset.
"""
import json
import re
import shutil
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "GratiaVR"
OUTPUT = ROOT / "evidence" / "04"
OUTPUT.mkdir(parents=True, exist_ok=True)
LIB = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
PROFILE_DIRECTORY = "/Game/Characters/Profiles"
REGENERATE = "-gratiaregenerateprofiles" in unreal.SystemLibrary.get_command_line().lower()
VALIDATION_SIGNATURE_LOGGED = False


def make(struct_type, **properties):
    value = struct_type()
    for key, item in properties.items():
        value.set_editor_property(key, item)
    return value


def set_properties(value, **properties):
    for key, item in properties.items():
        value.set_editor_property(key, item)


def require_asset(path, expected_type):
    asset = unreal.load_asset(path)
    if not isinstance(asset, expected_type):
        raise RuntimeError(f"Missing {expected_type.__name__} asset: {path}")
    return asset


def bone_names(mesh):
    component = unreal.SkeletalMeshComponent()
    component.set_skinned_asset_and_update(mesh)
    return [str(component.get_bone_name(index)) for index in range(component.get_num_bones())]


def create_asset(name):
    path = PROFILE_DIRECTORY + "/" + name
    existing = unreal.load_asset(path) if LIB.does_asset_exist(path) else None
    if existing:
        if not isinstance(existing, unreal.GratiaCharacterProfile):
            raise RuntimeError(f"Refusing to replace another asset type: {path}")
        return existing
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.GratiaCharacterProfile)
    result = TOOLS.create_asset(name, PROFILE_DIRECTORY, unreal.GratiaCharacterProfile, factory)
    if not result:
        raise RuntimeError(f"Cannot create profile: {path}")
    return result


def quality_settings(disabled=False):
    caps = [(0, 0, 4, 11, 15), (32, 16, 4, 11, 64), (150, 150, 150, 150, 256)]
    return [
        make(unreal.GratiaQualityProfile, name=name, hair_cap=0 if disabled else values[0],
             cloth_cap=0 if disabled else values[1], body_cap=0 if disabled else values[2],
             ear_cap=0 if disabled else values[3], total_body_cap=0 if disabled else values[4])
        for name, values in zip(("Low", "Medium", "High"), caps)
    ]


def zone(name, semantic, radius, priority, hold=True, offset=(0, 0, 0)):
    return make(unreal.GratiaContactZoneDefinition, name=name, bone_semantic=semantic,
                radius=radius, priority=priority, can_hold=hold, offset=unreal.Vector(*offset))


def humanoid_zones(has_hair):
    result = [
        zone("Left hand", "LeftHand", 7, 1), zone("Right hand", "RightHand", 7, 1),
        zone("Left forearm", "LeftForearm", 9, 2), zone("Right forearm", "RightForearm", 9, 2),
        zone("Left shoulder", "LeftUpperArm", 11, 3), zone("Right shoulder", "RightUpperArm", 11, 3),
        zone("Face", "Head", 12, 0, False, (0, 0, 11)),
        zone("Upper costume" if has_hair else "Upper torso", "UpperChest", 23, 5),
        zone("Clothed torso" if has_hair else "Torso", "Chest", 21, 6),
        zone("Waist fabric" if has_hair else "Waist", "Pelvis", 23, 7),
        zone("Left fabric" if has_hair else "Left thigh", "LeftThigh", 15, 8),
        zone("Right fabric" if has_hair else "Right thigh", "RightThigh", 15, 8),
    ]
    if has_hair:
        result.insert(7, zone("Hair", "Head", 16, 4, True, (0, 0, 27)))
    return result


def collision_proxies(forward_axis):
    def proxy(name, start, radius, end=None, start_offset=(0, 0, 0), end_offset=(0, 0, 0)):
        return make(
            unreal.GratiaCollisionProxyDefinition, name=name, start_bone_semantic=start,
            end_bone_semantic=end or "", radius=radius,
            shape=unreal.GratiaCollisionProxyShape.CAPSULE if end else unreal.GratiaCollisionProxyShape.SPHERE,
            start_offset=unreal.Vector(*start_offset), end_offset=unreal.Vector(*end_offset),
        )
    # Simple bounded starting geometry; verify against each mesh in the headset.
    # Feet use authored forward-axis offsets, unlike a fixed Gratia axis.
    foot_end = tuple(value * 4 for value in forward_axis)
    result = [
        proxy("Head", "Head", 11, start_offset=(0, 0, 11)),
        proxy("Torso", "Pelvis", 17, "UpperChest"),
        proxy("Pelvis", "Pelvis", 13),
    ]
    for side in ("Left", "Right"):
        result += [
            proxy(side + " upper arm", side + "UpperArm", 4.5, side + "Forearm"),
            proxy(side + " forearm", side + "Forearm", 4.5, side + "Hand"),
            proxy(side + " hand", side + "Hand", 5),
            proxy(side + " thigh", side + "Thigh", 7.5, side + "Shin"),
            proxy(side + " shin", side + "Shin", 5, side + "Foot"),
            proxy(side + " foot", side + "Foot", 5.5, side + "Toe", end_offset=foot_end),
        ]
    return result


def validate_and_save(profile):
    global VALIDATION_SIGNATURE_LOGGED
    if not VALIDATION_SIGNATURE_LOGGED:
        unreal.log("PROFILE_VALIDATION_PYTHON_SIGNATURE " + str(profile.validate_profile.__doc__))
        VALIDATION_SIGNATURE_LOGGED = True
    result = profile.validate_profile()
    # Unreal can treat a leading bool as a success gate: successful calls expose
    # only the two output arrays and failed calls return None. Other wrappers
    # include the boolean explicitly. Never turn an unknown/failure shape into success.
    if result is None:
        passed, errors, warnings = False, ["ValidateProfile returned None (Unreal success gate failed)."], []
    elif isinstance(result, (tuple, list)) and len(result) == 2:
        errors, warnings = result
        passed = not errors
    elif isinstance(result, (tuple, list)) and len(result) == 3 and isinstance(result[0], bool):
        passed, errors, warnings = result
    elif isinstance(result, (tuple, list)) and len(result) == 3 and isinstance(result[2], bool):
        errors, warnings, passed = result
    else:
        passed, errors, warnings = False, [f"Unsupported ValidateProfile result shape: {type(result).__name__}: {result!r}"], []
    report = {
        "asset": profile.get_path_name(), "id": str(profile.get_editor_property("profile_id")),
        "mesh": profile.get_editor_property("mesh").get_path_name(),
        "bone_count": len(bone_names(profile.get_editor_property("mesh"))),
        "morph_count": len(profile.get_editor_property("mesh").get_editor_property("morph_targets")),
        "zone_count": len(profile.get_editor_property("contact_zones")),
        "collision_proxy_count": len(profile.get_editor_property("collision_proxies")),
        "secondary_count": len(profile.get_editor_property("secondary_bones")),
        "valid": bool(passed), "errors": list(errors), "warnings": list(warnings),
    }
    if not passed:
        raise RuntimeError(f"Invalid profile {profile.get_name()}: {list(errors)}")
    if not LIB.save_loaded_asset(profile, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save profile {profile.get_name()}")
    return report


def create_gratia():
    existing_path = PROFILE_DIRECTORY + "/DA_Gratia"
    if not REGENERATE and LIB.does_asset_exist(existing_path):
        existing = require_asset(existing_path, unreal.GratiaCharacterProfile)
        report = validate_and_save(existing)
        report["existing_settings_preserved"] = True
        return report
    mesh = require_asset("/Game/Gratia/GameRig/SK_Gratia_Game", unreal.SkeletalMesh)
    profile = create_asset("DA_Gratia")
    secondary_header = (PROJECT / "Source/GratiaVREditorTools/GratiaSecondaryBones.h").read_text(encoding="utf-8")
    definitions = re.findall(
        r'\{\s*TEXT\("([^"]+)"\),\s*(\d+),\s*([\d.]+)f,\s*(true|false)\s*\}', secondary_header)
    if not definitions:
        raise RuntimeError("The audited secondary manifest is missing or empty.")
    secondary = [
        make(unreal.GratiaSecondaryBoneDefinition, bone=name, group=int(group),
             rest_length_cm=float(length), safe_simulation=safe == "true")
        for name, group, length, safe in definitions
    ]
    bones = {
        "Root": "root", "Head": "DEF-spine_006", "Neck": "DEF-spine_005",
        "UpperChest": "DEF-spine_003", "Chest": "DEF-spine_002", "Pelvis": "DEF-spine",
        "LeftHand": "DEF-hand_L", "RightHand": "DEF-hand_R",
        "LeftForearm": "DEF-forearm_L", "RightForearm": "DEF-forearm_R",
        "LeftUpperArm": "DEF-upper_arm_L", "RightUpperArm": "DEF-upper_arm_R",
        "LeftThigh": "DEF-thigh_L", "RightThigh": "DEF-thigh_R",
        "LeftShin": "DEF-shin_L", "RightShin": "DEF-shin_R",
        "LeftFoot": "DEF-foot_L", "RightFoot": "DEF-foot_R",
        "LeftToe": "DEF-toe_L", "RightToe": "DEF-toe_R",
    }
    morphs = {
        "BlinkLeft": "Eye L close", "BlinkRight": "Eye R close",
        "Smile": "Mouth smile", "BrowsUp": "Brows up", "Surprise": "Eyes surprised",
        "MouthOpen": "Mouth o", "MouthWide": "Mouth O wide",
        "LookLeft": "Look left", "LookRight": "Look right", "LookUp": "Look up", "LookDown": "Look down",
    }
    physics = mesh.get_editor_property("physics_asset")
    if not physics:
        raise RuntimeError("Build and save Gratia's PhysicsAsset before creating its profile.")
    properties = {
        "profile_id": "Gratia", "display_name": "Gratia", "mesh": mesh, "physics_asset": physics,
        "animation_class": None,
        "semantic_bones": bones, "semantic_morphs": morphs,
        "contact_zones": humanoid_zones(True), "secondary_bones": secondary,
        "collision_proxies": collision_proxies((0, 1, 0)),
        "quality_profiles": quality_settings(), "capabilities": unreal.GratiaCharacterCapabilities(),
        "forward_axis": unreal.Vector(0, 1, 0), "up_axis": unreal.Vector(0, 0, 1),
        "expected_bone_count": len(bone_names(mesh)),
        "expected_morph_count": len(mesh.get_editor_property("morph_targets")),
        # Model-specific regression values; they never constrain another character.
        "expected_physics_body_count": 176, "expected_constraint_count": 146,
        "require_planted_idle": True,
    }
    for property_name, clip_name in (
        ("idle", "Idle"), ("arms", "TestArms"), ("head", "TestHead"),
        ("react_soft", "ReactSoft"), ("react_bright", "ReactBright"),
    ):
        properties[property_name] = require_asset("/Game/Gratia/GameRig/A_Gratia_Game_" + clip_name, unreal.AnimSequence)
    set_properties(profile, **properties)
    return validate_and_save(profile)


def discover_mannequin():
    # Full-body assets only. XR mannequin hand meshes are deliberately excluded.
    candidates = [
        "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple",
        "/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple",
        "/Game/Mannequin/Character/Mesh/SK_Mannequin",
    ]
    for path in candidates:
        if LIB.does_asset_exist(path):
            return require_asset(path, unreal.SkeletalMesh), "existing project asset"

    installation = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.engine_dir())).parent
    source = installation / "Templates/TemplateResources/High/Characters/Content/Mannequins"
    if not (source / "Meshes/SKM_Manny_Simple.uasset").is_file():
        return None, "No full-body mannequin is available in the project or installed template resources."
    destination = PROJECT / "Content/Characters/Mannequins"
    copied = 0
    # Preserve the original /Game/Characters namespace so package references stay valid.
    # Existing files are never replaced.
    for original in source.rglob("*"):
        if not original.is_file():
            continue
        target = destination / original.relative_to(source)
        if target.exists():
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, target)
        copied += 1
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        ["/Game/Characters/Mannequins"], force_rescan=True)
    mesh = require_asset(candidates[0], unreal.SkeletalMesh)
    return mesh, f"Installed Epic UE template resources; {copied} new files copied, existing assets preserved."


def create_mannequin():
    existing_path = PROFILE_DIRECTORY + "/DA_Mannequin"
    if not REGENERATE and LIB.does_asset_exist(existing_path):
        existing = require_asset(existing_path, unreal.GratiaCharacterProfile)
        report = validate_and_save(existing)
        report.update(available=True, existing_settings_preserved=True, provenance="Existing profile preserved.")
        return report
    mesh, provenance = discover_mannequin()
    if not mesh:
        return {"available": False, "reason": provenance}
    available = set(bone_names(mesh))
    if not {"head", "hand_l", "hand_r", "foot_l", "foot_r"}.issubset(available):
        raise RuntimeError(f"Second model is not a compatible full-body humanoid: {mesh.get_path_name()}")
    profile = create_asset("DA_Mannequin")
    bones = {
        "Root": "root", "Head": "head", "Neck": "neck_01", "UpperChest": "spine_03",
        "Chest": "spine_02", "Pelvis": "pelvis", "LeftHand": "hand_l", "RightHand": "hand_r",
        "LeftForearm": "lowerarm_l", "RightForearm": "lowerarm_r",
        "LeftUpperArm": "upperarm_l", "RightUpperArm": "upperarm_r",
        "LeftThigh": "thigh_l", "RightThigh": "thigh_r", "LeftFoot": "foot_l", "RightFoot": "foot_r",
        "LeftShin": "calf_l", "RightShin": "calf_r", "LeftToe": "ball_l", "RightToe": "ball_r",
    }
    capabilities = make(unreal.GratiaCharacterCapabilities, blink=False, gaze=True,
                        facial_reactions=False, reaction_animations=False, contacts=True,
                        secondary_physics=False, local_springs=False, sound=True)
    idle = None
    for path in ("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle",):
        candidate = unreal.load_asset(path) if LIB.does_asset_exist(path) else None
        if isinstance(candidate, unreal.AnimSequence) and candidate.get_editor_property("skeleton") == mesh.get_editor_property("skeleton"):
            idle = candidate
            break
    set_properties(
        profile, profile_id="Mannequin", display_name="Epic Mannequin (replacement test)", mesh=mesh,
        physics_asset=mesh.get_editor_property("physics_asset"), animation_class=None,
        idle=idle, arms=None, head=None, react_soft=None, react_bright=None,
        semantic_bones=bones, semantic_morphs={}, contact_zones=humanoid_zones(False),
        collision_proxies=collision_proxies((1, 0, 0)),
        secondary_bones=[], quality_profiles=quality_settings(True), capabilities=capabilities,
        forward_axis=unreal.Vector(1, 0, 0), up_axis=unreal.Vector(0, 0, 1),
        expected_bone_count=0, expected_morph_count=0,
        expected_physics_body_count=0, expected_constraint_count=0,
        require_planted_idle=False,
    )
    report = validate_and_save(profile)
    report.update(available=True, provenance=provenance,
                  purpose="Actual second skeleton test; facial morphs, authored reactions and accessory physics intentionally unavailable.")
    return report


report = {"gratia": create_gratia(), "replacement": create_mannequin()}
(OUTPUT / "character_profiles.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
unreal.log("GRATIA_CHARACTER_PROFILES_CREATED " + json.dumps(report, ensure_ascii=False))
