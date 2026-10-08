"""Author the scene environments made from Fab packs; run by Build-Stage1.ps1 -RegenerateScenes.

Packs (Fab Standard License, git-ignored, restored from the Epic Games Launcher, see docs/EXPERIENCE.md):
  * /Game/Wabi_Sabi_Interior  (Wabi Sabi Boutique Guesthouse, Mint Studio): daylight interior lit by
    Lumen. Forward VR has no dynamic GI, so its Demo map is copied into an art level with static lights,
    a sky light from the pack's HDRI cubemap, an HDRI sphere behind the windows and a Lightmass volume;
    Build-Stage1 then bakes it (bake_fab_lighting step).
  * /Game/SoulCity  (Soul: City, Epic Games): night slum with baked mobile lighting. Its maps stream
    unmodified as backdrops, so the shipped lightmaps stay valid.

Each scene gets a small Gratia level (markers, speakers, music-reactive lights); the pack levels stream
with it as FGratiaSceneEntry::Backdrops. Writes evidence/experience/fab_environments.json, read by
setup_scene_experience.py.
"""
import json
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'evidence/experience'
OUT.mkdir(parents=True, exist_ok=True)
ENV = '/Game/Gratia/Experience/Environments'
LIB = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LEVELS = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EDIT = unreal.MaterialEditingLibrary

WABI_DEMO = '/Game/Wabi_Sabi_Interior/Maps/Demo'
WABI_SKY = '/Game/Wabi_Sabi_Interior/HDRI/mint0165'
SOUL_MAP = '/Game/SoulCity/Maps/LV_Soul_Slum_Mobile'
SOUL_COLLISION = '/Game/SoulCity/Maps/SLV_Soul_Slum_Collision'
for required in (WABI_DEMO, WABI_SKY, SOUL_MAP, SOUL_COLLISION):
    assert LIB.does_asset_exist(required), f'Fab pack asset missing: {required} (download the packs, see docs/EXPERIENCE.md)'

_forward = unreal.load_asset('/Game/Characters/Profiles/DA_Gratia').get_editor_property('forward_axis')
FORWARD_YAW = __import__('math').degrees(__import__('math').atan2(_forward.y, _forward.x))
PINK = (1.0, 0.06, 0.42)
CYAN = (0.05, 0.62, 1.0)
WARM = (1.0, 0.72, 0.45)


def color(rgb, a=1.0):
    return unreal.LinearColor(rgb[0], rgb[1], rgb[2], a)


def props(obj, **values):
    for key, value in values.items():
        obj.set_editor_property(key, value)
    return obj


def vec(p):
    return unreal.Vector(float(p[0]), float(p[1]), float(p[2]))


def label(actor, name, tags=()):
    actor.set_actor_label('Experience_' + name)
    if tags:
        actor.set_editor_property('tags', [unreal.Name(t) for t in tags])
    return actor


def fresh_level(path):
    """Gratia level of a scene: regenerated from scratch (only markers, speakers and lights live here)."""
    if LIB.does_asset_exist(path):
        assert LEVELS.load_level(path)
        for actor in ACTORS.get_all_level_actors():
            if actor.get_actor_label().startswith('Experience_'):
                assert ACTORS.destroy_actor(actor)
    else:
        assert LEVELS.new_level(path)
    return path


def ground(world, x, y, top, depth=1500.0):
    """Floor height under (x, y): the first blocking hit straight down from top (below awnings)."""
    hit = unreal.SystemLibrary.line_trace_single(world, vec((x, y, top)), vec((x, y, top - depth)), unreal.TraceTypeQuery.ECC_VISIBILITY,
                                                 False, [], unreal.DrawDebugTrace.NONE, True)
    if not hit:
        return None
    return float(hit.to_tuple()[4].z)


def marker(tag, position, yaw):
    actor = ACTORS.spawn_actor_from_class(unreal.TargetPoint, vec(position), unreal.Rotator(pitch=0, yaw=yaw, roll=0))
    return label(actor, tag, (tag,))


def light(name, position, rgb, intensity, radius, tags=()):
    actor = ACTORS.spawn_actor_from_class(unreal.PointLight, vec(position))
    component = actor.get_component_by_class(unreal.LocalLightComponent)
    props(component, intensity=float(intensity), cast_shadows=False, attenuation_radius=float(radius))
    component.set_light_color(color(rgb))
    component.set_mobility(unreal.ComponentMobility.MOVABLE)
    return label(actor, name, tags)


def stand(character, player, floor_z):
    """Character faces the player; the player looks at her. Speakers stand behind her, left and right."""
    import math
    cx, cy = character
    px, py = player
    to_player = math.degrees(math.atan2(py - cy, px - cx))
    # The character's front is the profile's ForwardAxis in actor space (Gratia: +Y).
    marker('GratiaCharacterSpot', (cx, cy, floor_z), to_player - FORWARD_YAW)
    marker('GratiaPlayerSpot', (px, py, floor_z), to_player + 180.0)
    back = math.radians(to_player + 180.0)
    side = math.radians(to_player + 90.0)
    for name, sign in (('GratiaSpeakerL', -1.0), ('GratiaSpeakerR', 1.0)):
        x = cx + math.cos(back) * 150.0 + math.cos(side) * sign * 220.0
        y = cy + math.sin(back) * 150.0 + math.sin(side) * sign * 220.0
        marker(name, (x, y, floor_z + 140.0), to_player)


# ------------------------------------------------------------------------------- Wabi Sabi art level
def window_sky_material():
    path = ENV + '/Fab/M_WindowSky'
    if LIB.does_asset_exist(path):
        material = unreal.load_asset(path)
        EDIT.delete_all_material_expressions(material)
    else:
        material = TOOLS.create_asset('M_WindowSky', ENV + '/Fab', unreal.Material, unreal.MaterialFactoryNew())
    props(material, shading_model=unreal.MaterialShadingModel.MSM_UNLIT, two_sided=True, used_with_static_lighting=True)
    view = EDIT.create_material_expression(material, unreal.MaterialExpressionCameraVectorWS, -600, 0)
    flip = EDIT.create_material_expression(material, unreal.MaterialExpressionMultiply, -450, 0)
    props(flip, const_b=-1.0)
    EDIT.connect_material_expressions(view, '', flip, 'A')
    sky = EDIT.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameterCube, -300, 0)
    props(sky, parameter_name='Sky', texture=unreal.load_asset(WABI_SKY))
    EDIT.connect_material_expressions(flip, '', sky, 'UVs')
    exposure = EDIT.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -300, 200)
    props(exposure, parameter_name='Exposure', default_value=1.0)
    out = EDIT.create_material_expression(material, unreal.MaterialExpressionMultiply, -100, 0)
    EDIT.connect_material_expressions(sky, 'RGB', out, 'A')
    EDIT.connect_material_expressions(exposure, '', out, 'B')
    EDIT.connect_material_property(out, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    EDIT.recompile_material(material)
    LIB.save_loaded_asset(material)
    return material


# Lumen-made materials are not flagged for baked lighting; a cooked/-game build cannot add usage flags
# and falls back to the default checker material.
flagged = 0
for path in LIB.list_assets('/Game/Wabi_Sabi_Interior/Materials', recursive=True, include_folder=False):
    asset = unreal.load_asset(path)
    if isinstance(asset, unreal.Material) and not asset.get_editor_property('used_with_static_lighting'):
        asset.set_editor_property('used_with_static_lighting', True)
        EDIT.recompile_material(asset)
        LIB.save_loaded_asset(asset, only_if_is_dirty=False)
        flagged += 1

# Arch-viz density: the guesthouse meshes have no LODs and up to 880k triangles each (5.4 M in all), about
# 5 ms of the VR frame on an RTX 3060. LOD 0 of every mesh is reduced to at most WABI_MAX_TRIANGLES (and
# at least 5 % of its source); the art level is re-baked afterwards (its lightmaps match the old geometry).
WABI_MAX_TRIANGLES = 40000
reduced_meshes, wabi_triangles = 0, 0
for path in LIB.list_assets('/Game/Wabi_Sabi_Interior/Geometries', recursive=True, include_folder=False):
    mesh = unreal.load_asset(path)
    if not isinstance(mesh, unreal.StaticMesh):
        continue
    if LIB.get_metadata_tag(mesh, 'GratiaMaxTriangles') != str(WABI_MAX_TRIANGLES):
        source = int(LIB.get_metadata_tag(mesh, 'GratiaSourceTriangles') or mesh.get_num_triangles(0))
        percent = 1.0 if source <= WABI_MAX_TRIANGLES else max(0.05, WABI_MAX_TRIANGLES / source)
        if percent < 1.0 or LIB.get_metadata_tag(mesh, 'GratiaMaxTriangles'):
            assert unreal.GratiaExperienceToolsLibrary.reduce_static_mesh_lod0(mesh, percent) > 0, f'Reduction failed: {path}'
            reduced_meshes += 1
        LIB.set_metadata_tag(mesh, 'GratiaSourceTriangles', str(source))
        LIB.set_metadata_tag(mesh, 'GratiaMaxTriangles', str(WABI_MAX_TRIANGLES))
        assert LIB.save_loaded_asset(mesh, only_if_is_dirty=False)
    wabi_triangles += mesh.get_num_triangles(0)

# Its walls and partitions fill most of the view with 4K textures: in VR at room distance 2K carries the
# same detail at a fraction of the bandwidth (the base pass of the guesthouse is fill-rate bound).
WABI_MAX_TEXTURE = 2048
capped_textures = 0
for path in LIB.list_assets('/Game/Wabi_Sabi_Interior/Textures', recursive=True, include_folder=False):
    texture = unreal.load_asset(path)
    if not isinstance(texture, unreal.Texture2D) or LIB.get_metadata_tag(texture, 'GratiaMaxTextureSize') == str(WABI_MAX_TEXTURE):
        continue
    if max(texture.blueprint_get_size_x(), texture.blueprint_get_size_y()) > WABI_MAX_TEXTURE:
        texture.set_editor_property('max_texture_size', WABI_MAX_TEXTURE)
        capped_textures += 1
    LIB.set_metadata_tag(texture, 'GratiaMaxTextureSize', str(WABI_MAX_TEXTURE))
    assert LIB.save_loaded_asset(texture, only_if_is_dirty=False)

WABI_ART = ENV + '/Fab/L_WabiSabi_Art'
if reduced_meshes:
    stamp = ROOT / 'GratiaVR/Saved/FabBake' / (WABI_ART.rsplit('/', 1)[-1] + '.stamp')
    if stamp.is_file():
        stamp.unlink()
sky_material = window_sky_material()
# The art level (and its baked light) is kept once made: rebuilding it drops the lightmaps. Delete
# L_WabiSabi_Art to re-author it from the pack's Demo map; Build-Stage1 then bakes it again.
removed, baked_lights = 0, 0
if not LIB.does_asset_exist(WABI_ART):
    assert LIB.duplicate_asset(WABI_DEMO, WABI_ART)
    assert LEVELS.load_level(WABI_ART)
    # The pack is a Lumen project: its world forbids precomputed lighting, which is what the bake makes here.
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_world_settings().set_editor_property('force_no_precomputed_lighting', False)
    for actor in ACTORS.get_all_level_actors():
        cls = actor.get_class().get_name()
        # Cinematic cameras and their targets of the pack's showcase, and the editor-only HDRI backdrop.
        if cls in ('CineCameraActor',) or (cls == 'Actor' and actor.get_actor_label().endswith('_Target')) or 'HDRIBackdrop' in cls:
            assert ACTORS.destroy_actor(actor)
            removed += 1
            continue
        for component in actor.get_components_by_class(unreal.LightComponent):
            component.set_mobility(unreal.ComponentMobility.STATIC)
            baked_lights += 1
        for component in actor.get_components_by_class(unreal.StaticMeshComponent):
            component.set_mobility(unreal.ComponentMobility.STATIC)
    skylight = ACTORS.spawn_actor_from_class(unreal.SkyLight, vec((-150, 120, 200)))
    props(skylight.light_component, source_type=unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP, cubemap=unreal.load_asset(WABI_SKY),
          intensity=1.0, lower_hemisphere_is_black=False)
    skylight.light_component.set_mobility(unreal.ComponentMobility.STATIC)
    label(skylight, 'SkyLight')
    sphere = ACTORS.spawn_actor_from_class(unreal.StaticMeshActor, vec((-150, 120, 150)))
    sphere.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Sphere'))
    sphere.static_mesh_component.set_material(0, sky_material)
    props(sphere.static_mesh_component, cast_shadow=False)
    sphere.static_mesh_component.set_collision_profile_name('NoCollision')
    sphere.set_actor_scale3d(vec((60, 60, 60)))  # 30 m radius around the room
    label(sphere, 'WindowSky')
    volume = ACTORS.spawn_actor_from_class(unreal.LightmassImportanceVolume, vec((-150, 120, 150)))
    volume.set_actor_scale3d(vec((6.5, 7.0, 2.2)))  # default brush 200 cm: 13 x 14 x 4.4 m around the room
    label(volume, 'LightmassVolume')
    assert LEVELS.save_current_level()

def scene_level(name, character, player, floor_z, lights, grade=None):
    """Gratia level of one scene: markers, speakers, music-reactive lights (and an optional grade)."""
    path = fresh_level(ENV + '/' + name)
    stand(character, player, floor_z)
    for args in lights:
        light(*args)
    if grade:
        grade()
    assert LEVELS.save_current_level()
    return path


def wabi_grade():
    # The pack's volume uses convolution (FFT) bloom: 4.4 ms of the VR frame on an RTX 3060. A light
    # standard bloom keeps the window glow; fixed exposure like every scene.
    grade = ACTORS.spawn_actor_from_class(unreal.PostProcessVolume, vec((0, 0, 0)))
    props(grade, unbound=True, priority=10.0)
    pp = grade.get_editor_property('settings')
    props(pp, override_bloom_method=True, bloom_method=unreal.BloomMethod.BM_SOG,
          override_bloom_intensity=True, bloom_intensity=0.15, override_bloom_threshold=True, bloom_threshold=1.0,
          override_lens_flare_intensity=True, lens_flare_intensity=0.0,
          override_auto_exposure_method=True, auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,
          override_auto_exposure_apply_physical_camera_exposure=True, auto_exposure_apply_physical_camera_exposure=False,
          override_auto_exposure_bias=True, auto_exposure_bias=0.0)
    grade.set_editor_property('settings', pp)
    label(grade, 'Grade')


# Free play: the clear strip between the dining area and the shelves (see docs/EXPERIENCE.md); she
# stands by the shelves with the window behind, the player looks along +X from the hall.
WABI = scene_level('L_WabiSabi', (130.0, 185.0), (-20.0, 185.0), 0.0, [
    ('PendantA', (108, -39, 175), WARM, 6.0, 450, ('GratiaAudioLight',)),
    ('PendantB', (-102, -17, 175), WARM, 6.0, 450, ('GratiaAudioLight', 'GratiaAudioMid')),
    ('Key', (20, 185, 230), (1.0, 0.86, 0.75), 4.0, 400)], wabi_grade)

# ------------------------------------------------------------------------------- Soul: City
# The night was lit for UE 4.19 eye adaptation (several EV of automatic brightening). GratiaVR keeps a
# fixed exposure and the character is self-lit for exposure 0 (toon emissive), so the pack's baked lights
# are scaled once instead; the map is then re-baked (the stamp below is dropped).
SOUL_LIGHT_SCALE = 10.0
assert LEVELS.load_level(SOUL_MAP)
soul_world = unreal.load_asset(SOUL_MAP)
if LIB.get_metadata_tag(soul_world, 'GratiaLightScale') != str(SOUL_LIGHT_SCALE):
    previous = float(LIB.get_metadata_tag(soul_world, 'GratiaLightScale') or 1.0)
    for actor in ACTORS.get_all_level_actors():
        for component in actor.get_components_by_class(unreal.LightComponentBase):
            component.set_editor_property('intensity', component.get_editor_property('intensity') * SOUL_LIGHT_SCALE / previous)
    LIB.set_metadata_tag(soul_world, 'GratiaLightScale', str(SOUL_LIGHT_SCALE))
    assert LEVELS.save_current_level()
    stamp = ROOT / 'GratiaVR/Saved/FabBake' / (SOUL_MAP.rsplit('/', 1)[-1] + '.stamp')
    if stamp.is_file():
        stamp.unlink()
# Ground heights come from the pack's own collision: trace before building the scene level.
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
SOUL_EXPOSURE_EV = 0.0
# The open plaza in the middle of the quarter (layout scout, 8 x 9 m of flat paving at z ~184): the
# player looks north at the neon facades; behind her there is room for a performance partner lying on
# the floor (KM466 puts him 0.3-2.3 m behind her). Overhead beams start at z 850, so traces start below.
SOUL_CHARACTER, SOUL_PLAYER = (1950.0, -950.0), (1800.0, -950.0)
heights = [ground(world, *SOUL_CHARACTER, 400.0), ground(world, *SOUL_PLAYER, 400.0)]
assert all(h is not None and 150.0 < h < 220.0 for h in heights), f'Unexpected ground under the Soul: City markers: {heights}'
soul_floor = max(heights)


def soul_grade():
    # The pack's post-process volume was tuned for UE 4.19 auto exposure; this one wins (higher priority).
    grade = ACTORS.spawn_actor_from_class(unreal.PostProcessVolume, vec((0, 0, 0)))
    props(grade, unbound=True, priority=10.0)
    pp = grade.get_editor_property('settings')
    # The pack's 4.19 grade had strong bloom and lens flares around the self-lit character; bloom also costs
    # 1.7 ms of the VR frame, and the neon reads without it.
    props(pp, override_bloom_intensity=True, bloom_intensity=0.0,
          override_lens_flare_intensity=True, lens_flare_intensity=0.0,
          # Fixed exposure like every scene (the pack's 4.19 eye adaptation is replaced by brighter baked light).
          override_auto_exposure_method=True, auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,
          override_auto_exposure_apply_physical_camera_exposure=True, auto_exposure_apply_physical_camera_exposure=False,
          override_auto_exposure_bias=True, auto_exposure_bias=SOUL_EXPOSURE_EV)
    grade.set_editor_property('settings', pp)
    label(grade, 'Grade')


def soul_neon(character, floor_z):
    # Pink and cyan neon on the walls around her follow the music (the character is self-lit, unaffected).
    return [('NeonPink', (character[0] + 120, character[1] - 260, floor_z + 320), PINK, 6.0, 700, ('GratiaAudioLight',)),
            ('NeonCyan', (character[0] + 120, character[1] + 260, floor_z + 320), CYAN, 6.0, 700, ('GratiaAudioLight', 'GratiaAudioHigh'))]


SOUL = scene_level('L_SoulCity', SOUL_CHARACTER, SOUL_PLAYER, soul_floor, soul_neon(SOUL_CHARACTER, soul_floor), soul_grade)


def soul_rain():
    """Rain over the plaza as the pack's own desktop map places it (its mobile map has no rain and the
    mobile storm emits nothing): storms ~7 m above the paving every ~8 m, splashes on the paving, the loop."""
    soul_grade()
    rain = unreal.load_asset('/Game/SoulCity/Effects/Particles/Water/P_RainStorm')
    splash = unreal.load_asset('/Game/SoulCity/Effects/Particles/Water/P_RainGroundSplash')
    loop = unreal.load_asset('/Game/SoulCity/Sound/Cue/AmbientLooping/Rain_Heavy_Ext_Cue')
    assert rain and splash and loop, 'Soul: City rain assets are missing'
    cx, cy = (SOUL_CHARACTER[0] + SOUL_PLAYER[0]) / 2, (SOUL_CHARACTER[1] + SOUL_PLAYER[1]) / 2

    def emitter(name, system, position):
        actor = ACTORS.spawn_actor_from_class(unreal.Emitter, vec(position))
        component = actor.get_component_by_class(unreal.ParticleSystemComponent)
        component.set_template(system)
        component.set_editor_property('auto_activate', True)
        label(actor, name)

    for i, (dx, dy) in enumerate(((-400, -400), (-400, 400), (400, -400), (400, 400))):
        emitter(f'Rain{i}', rain, (cx + dx, cy + dy, soul_floor + 700))
    # The pack's own splash spots on this plaza, then around her and the player.
    spots = [(1870, -780), (2230, -770), (2610, -740), (1830, -1060), (1720, -1550)]
    spots += [(SOUL_CHARACTER[0] + dx, SOUL_CHARACTER[1] + dy) for dx, dy in ((0, -250), (0, 250), (250, 0))]
    spots += [(SOUL_PLAYER[0] + dx, SOUL_PLAYER[1] + dy) for dx, dy in ((-150, -200), (-150, 200))]
    for i, (x, y) in enumerate(spots):
        emitter(f'RainSplash{i}', splash, (x, y, soul_floor + 6))
    sound = ACTORS.spawn_actor_from_class(unreal.AmbientSound, vec((cx, cy, soul_floor + 300)))
    audio = sound.get_component_by_class(unreal.AudioComponent)
    audio.set_sound(loop)
    props(audio, volume_multiplier=0.35, auto_activate=True)
    label(sound, 'RainLoop')


# The same plaza in the rain (its own card: choosing it is the rain switch; MVP keeps rain optional).
SOUL_RAIN = scene_level('L_SoulCity_Rain', SOUL_CHARACTER, SOUL_PLAYER, soul_floor, soul_neon(SOUL_CHARACTER, soul_floor), soul_rain)


def baked(path):
    """Light baked on this machine for the current art (Build-Stage1 stamps Saved/FabBake after a bake).

    Soul: City ships lightmaps from UE 4.19 that 5.8 rejects after its meshes are rebuilt, so the pack
    map is re-baked once per machine; the Wabi Sabi art level is baked when it is (re)made.
    """
    stamp = ROOT / 'GratiaVR/Saved/FabBake' / (path.rsplit('/', 1)[-1] + '.stamp')
    return stamp.is_file() and LIB.does_asset_exist(path + '_BuiltData')


report = dict(
    scenes={
        'WabiSabi': dict(environment=WABI, backdrops=[WABI_ART]),
        'SoulCity': dict(environment=SOUL, backdrops=[SOUL_MAP, SOUL_COLLISION]),
        'SoulCityRain': dict(environment=SOUL_RAIN, backdrops=[SOUL_MAP, SOUL_COLLISION]),
    },
    wabi_art=dict(removed_showcase_actors=removed, static_lights=baked_lights, materials_flagged_static_lighting=flagged,
                  meshes_reduced=reduced_meshes, lod0_triangles=wabi_triangles, textures_capped=capped_textures),
    # Lightmass quality per map: the small guesthouse at Production, the large city at Medium (~35 min).
    soul_floor_z=soul_floor, bake=[dict(map=path, quality=quality) for path, quality in ((WABI_ART, 'Production'), (SOUL_MAP, 'Medium'))
                                   if not baked(path)])
(OUT / 'fab_environments.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('GRATIA_FAB_ENVIRONMENTS_READY ' + json.dumps(report, ensure_ascii=False))
