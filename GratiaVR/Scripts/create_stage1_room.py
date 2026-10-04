"""Build the first VR room with the editor Python API. No model import."""
import json
import datetime
import shutil
from pathlib import Path
import unreal

MAP = '/Game/Gratia/Maps/L_Stage1'
OUT = Path(r'E:\coding\ue proto\evidence\01')
OUT.mkdir(parents=True, exist_ok=True)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if unreal.EditorAssetLibrary.does_asset_exist(MAP):
    previous_map = Path(unreal.Paths.project_content_dir()) / 'Gratia/Maps/L_Stage1.umap'
    if previous_map.exists():
        stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        shutil.copy2(previous_map, OUT / f'L_Stage1_before_rebuild_{stamp}.umap')
    assert levels.load_level(MAP), 'Could not load existing stage 1 map'
    generated_labels = {
        'Floor_6m', 'BackWall', 'LeftWall', 'RightWall', 'ScaleCube_Exactly1m',
        'HeightMarker_165cm', 'Stage1Instructions', 'ScaleCubeLabel', 'HeightMarkerLabel',
        'VR_FloorOrigin', 'DirectionalLight', 'SkyLight', 'Stage1Sun', 'Stage1Sky',
        'Stage1CalibrationAndTrackingGuard',
    }
    for actor in actors.get_all_level_actors():
        if actor.get_actor_label() in generated_labels:
            assert actors.destroy_actor(actor), 'Could not replace generated room actor'
else:
    assert levels.new_level(MAP), 'Could not create stage 1 map'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
cube_mesh = unreal.load_asset('/Engine/BasicShapes/Cube')
assert cube_mesh

def material(name, rgb):
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    asset = unreal.load_asset('/Game/Gratia/Materials/' + name)
    if not asset:
        asset = tools.create_asset(name, '/Game/Gratia/Materials', unreal.Material, unreal.MaterialFactoryNew())
    edit = unreal.MaterialEditingLibrary
    edit.delete_all_material_expressions(asset)
    color = edit.create_material_expression(asset, unreal.MaterialExpressionConstant3Vector, -300, 0)
    color.set_editor_property('constant', unreal.LinearColor(*rgb, 1))
    edit.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
    roughness = edit.create_material_expression(asset, unreal.MaterialExpressionConstant, -300, 200)
    roughness.set_editor_property('r', .85)
    edit.connect_material_property(roughness, '', unreal.MaterialProperty.MP_ROUGHNESS)
    edit.recompile_material(asset)
    unreal.EditorAssetLibrary.save_loaded_asset(asset)
    return asset

floor_mat = material('M_Stage1_Floor', (.10, .13, .17))
wall_mat = material('M_Stage1_Wall', (.30, .36, .42))
scale_mat = material('M_Stage1_Scale', (.02, .65, .60))

def box(name, location, scale, mat):
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*location))
    actor.set_actor_label(name)
    component = actor.static_mesh_component
    component.set_static_mesh(cube_mesh)
    component.set_material(0, mat)
    component.set_collision_profile_name('BlockAll')
    actor.set_actor_scale3d(unreal.Vector(*scale))
    return actor

box('Floor_6m', (0, 0, -10), (6, 6, .2), floor_mat)
box('BackWall', (300, 0, 140), (.1, 6, 2.8), wall_mat)
box('LeftWall', (0, -300, 140), (6, .1, 2.8), wall_mat)
box('RightWall', (0, 300, 140), (6, .1, 2.8), wall_mat)
box('ScaleCube_Exactly1m', (155, 150, 50), (1, 1, 1), scale_mat)
box('HeightMarker_165cm', (245, -170, 82.5), (.05, .05, 1.65), scale_mat)

def text(name, content, xyz, size=8):
    actor = actors.spawn_actor_from_class(unreal.TextRenderActor, unreal.Vector(*xyz), unreal.Rotator(pitch=0, yaw=180, roll=0))
    actor.set_actor_label(name)
    component = actor.get_component_by_class(unreal.TextRenderComponent)
    component.set_text(content)
    component.set_world_size(size)
    component.set_text_render_color(unreal.Color(220, 240, 255, 255))
    component.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
    return actor

text('Stage1Instructions', 'GRATIA VR / STAGE 1\nEmpty tracking and scale test\nR: recenter | PageUp / PageDown: height\nF1: debug | F8/F9: simulate hand tracking loss\nSteamVR first, then launch the VR build', (280, 0, 260), 9)
text('ScaleCubeLabel', '1 metre cube', (154, 150, 120), 7)
text('HeightMarkerLabel', '1.65 m', (240, -170, 175), 7)

start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0,0,0), unreal.Rotator(0,0,0))
start.set_actor_label('VR_FloorOrigin')
sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0,0,250), unreal.Rotator(pitch=-50, yaw=-25, roll=0))
sun.set_actor_label('Stage1Sun')
sun.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sun.light_component.set_editor_property('intensity', 3.0)
sun.light_component.set_editor_property('cast_shadows', False)
sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0,0,200))
sky.set_actor_label('Stage1Sky')
sky.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sky.light_component.set_editor_property('intensity', .7)
sky.light_component.set_editor_property('lower_hemisphere_is_black', False)

runtime_class = unreal.load_class(None, '/Script/GratiaVR.GratiaStage1Runtime')
game_class = unreal.load_class(None, '/Script/GratiaVR.GratiaStage1GameMode')
assert runtime_class and game_class, 'Build GratiaVR editor module before creating map'
runtime = actors.spawn_actor_from_class(runtime_class, unreal.Vector(0,0,0))
runtime.set_actor_label('Stage1CalibrationAndTrackingGuard')
runtime.set_editor_property('show_debug', False)
world.get_world_settings().set_editor_property('default_game_mode', game_class)
assert levels.save_current_level(), 'Map save failed'
unreal.EditorAssetLibrary.save_directory('/Game/Gratia', only_if_is_dirty=True, recursive=True)
report = {
    'map': MAP, 'room_metres': [6,6,2.8], 'reference_cube_metres': [1,1,1],
    'game_mode': str(game_class), 'runtime_actor': str(runtime_class),
    'actors': [{'name': a.get_actor_label(), 'class': a.get_class().get_name()} for a in actors.get_all_level_actors()],
    'vr_test': 'pending: headset intentionally deferred by user',
}
(OUT / 'room_manifest.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('GRATIA_STAGE1_ROOM_CREATED')

