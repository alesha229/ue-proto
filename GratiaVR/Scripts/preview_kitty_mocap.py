"""HISTORICAL (rejected KM466 v2 trial, superseded by author_vam_mocap.py); do not run.

Render the isolated KM466 trial in live Blender MCP; restore scene settings."""
import bpy
import json
from pathlib import Path
from mathutils import Vector

ROOT=Path('E:/coding/ue proto')
OUT=ROOT/'evidence/05/kitty_mocap_v2'
data=json.loads((OUT/'gratia_mocap_validation.json').read_text(encoding='utf-8'))
previous_scene=bpy.context.window.scene
bpy.context.window.scene=bpy.data.scenes[data['scene']]
scene=bpy.context.scene
game=bpy.data.objects[data['game_rig']]
selected=[bpy.data.objects[p['candidate']] for p in data['pairs']]
VIDEO=globals().get('VIDEO',False)
render_props=['engine','filepath','resolution_x','resolution_y','resolution_percentage','film_transparent']
saved={p:getattr(scene.render,p) for p in render_props}
saved.update(camera=scene.camera,frame=scene.frame_current,subframe=scene.frame_subframe)
shading=scene.display.shading
shade_props=['light','color_type','show_shadows','show_cavity','background_type','background_color','show_specular_highlight']
shade_saved={p:tuple(getattr(shading,p)) if p=='background_color' else getattr(shading,p) for p in shade_props}
format_saved=scene.render.image_settings.file_format
hide={o.name:o.hide_render for o in scene.objects}
action=game.animation_data.action
shape_actions={o.name:o.data.shape_keys.animation_data.action for o in selected}
camera_data=bpy.data.cameras.new('KM466_TrialPreviewCam');camera=bpy.data.objects.new(camera_data.name,camera_data);scene.collection.objects.link(camera)
floor_mesh=bpy.data.meshes.new('KM466_TrialPreviewFloor');floor_mesh.from_pydata([(-3,-3,0),(3,-3,0),(3,3,0),(-3,3,0)],[],[(0,1,2,3)])
floor=bpy.data.objects.new(floor_mesh.name,floor_mesh);scene.collection.objects.link(floor);floor.color=(.18,.2,.25,1)
try:
    scene.camera=camera;scene.render.engine='BLENDER_WORKBENCH'
    scene.render.resolution_x=scene.render.resolution_y=640;scene.render.resolution_percentage=100;scene.render.film_transparent=False
    scene.render.image_settings.file_format='PNG'
    shading.light='STUDIO';shading.color_type='MATERIAL';shading.show_shadows=True;shading.show_cavity=True
    shading.background_type='WORLD';shading.background_color=(.045,.055,.08);shading.show_specular_highlight=True
    camera_data.type='ORTHO';camera_data.ortho_scale=2.05
    camera.location=(2.3,-4.2,2.1);camera.rotation_euler=(Vector((0,0,.8))-camera.location).to_track_quat('-Z','Y').to_euler()
    for o in scene.objects:
        if o.type=='MESH':o.hide_render=o not in selected and o!=floor
    for o in selected:
        assert all(m.show_render == (m.type=='ARMATURE') for m in o.modifiers), o.name
    game.animation_data.action=bpy.data.actions[data['game_action']]
    for o in selected:o.data.shape_keys.animation_data.action=bpy.data.actions[data['shape_actions'][o.name]]
    rendered=[]
    frames=list(range(1,301,3)) if VIDEO else [1,151,301]
    for index,frame in enumerate(frames):
        scene.frame_set(frame);bpy.context.view_layer.update()
        path=OUT/('preview_frames' if VIDEO else 'preview_stills')/f'{index:04}.png';path.parent.mkdir(parents=True,exist_ok=True)
        scene.render.filepath=str(path);bpy.ops.render.render(write_still=True);rendered.append(str(path))
    result={'rendered':len(rendered),'first':rendered[0],'last':rendered[-1]}
finally:
    game.animation_data.action=action
    for o in selected:o.data.shape_keys.animation_data.action=shape_actions[o.name]
    for name,value in hide.items():bpy.data.objects[name].hide_render=value
    scene.camera=saved['camera']
    for p in render_props:setattr(scene.render,p,saved[p])
    for p in shade_props:setattr(shading,p,shade_saved[p])
    scene.render.image_settings.file_format=format_saved
    scene.frame_set(saved['frame'],subframe=saved['subframe']);bpy.context.view_layer.update()
    bpy.data.objects.remove(camera,do_unlink=True);bpy.data.cameras.remove(camera_data)
    bpy.data.objects.remove(floor,do_unlink=True);bpy.data.meshes.remove(floor_mesh)

    bpy.context.window.scene=previous_scene
