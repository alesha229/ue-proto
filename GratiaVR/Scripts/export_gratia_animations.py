"""Author three small FK clips in a disposable Blender session; never save .blend."""
import bpy
import json
import math
from pathlib import Path
from mathutils import Quaternion, Vector

ROOT = Path(r'E:\coding\ue proto')
OUT = ROOT / 'Exports/Gratia'
rig = bpy.data.objects['Gratia']
meshes = [o for o in bpy.data.objects if o.type == 'MESH' and o.parent == rig
          and not o.hide_render and o.name != 'Eyes horny']
bpy.data.objects['Face'].data.shape_keys.key_blocks['Mouth O'].name = 'Mouth O wide'
for obj in bpy.context.scene.objects:
    obj.select_set(False)
for obj in [rig] + meshes:
    obj.hide_set(False)
    obj.hide_viewport = False
    obj.select_set(True)
    if obj.type == 'MESH':
        # Probe the same base mesh/armature deformation that is exported.
        for modifier in obj.modifiers:
            modifier.show_viewport = modifier.type == 'ARMATURE'
        if obj.data.shape_keys:
            obj.data.shape_keys.animation_data_clear()
            for key in obj.data.shape_keys.key_blocks:
                key.value = 0
bpy.context.view_layer.objects.active = rig
rig.animation_data_create()
for side in ('L', 'R'):
    prop = rig.pose.bones['upper_arm_parent.' + side]
    assert 'IK_FK' in prop
    prop['IK_FK'] = 1.0
    prop.id_data.update_tag()
scene = bpy.context.scene
scene.render.fps = 30
scene.render.fps_base = 1
animated = ['upper_arm_fk.L', 'upper_arm_fk.R', 'forearm_fk.L', 'forearm_fk.R', 'spine_fk.003', 'head']
for name in animated:
    rig.pose.bones[name].rotation_mode = 'QUATERNION'

def rotation(name, world_axis, degrees):
    bone = rig.pose.bones[name]
    local_axis = bone.bone.matrix_local.to_quaternion().inverted() @ Vector(world_axis)
    return Quaternion(local_axis, math.radians(degrees))

def pose(arm_extra=0, elbow_extra=0, head_yaw=0, head_tilt=0, chest=0):
    for side, sign in (('L', 1), ('R', -1)):
        upper = 'upper_arm_fk.' + side
        forearm = 'forearm_fk.' + side
        rig.pose.bones[upper].rotation_quaternion = rotation(upper, (0, 1, 0), sign * (18 + arm_extra))
        rig.pose.bones[forearm].rotation_quaternion = rotation(forearm, (1, 0, 0), -5 - elbow_extra)
    rig.pose.bones['spine_fk.003'].rotation_quaternion = rotation('spine_fk.003', (1, 0, 0), chest)
    rig.pose.bones['head'].rotation_quaternion = rotation('head', (0, 0, 1), head_yaw) @ rotation('head', (1, 0, 0), head_tilt)

def probe(frame):
    scene.frame_set(frame)
    bpy.context.view_layer.update()
    evaluated = rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
    points = {name: list(evaluated.pose.bones[name].matrix.translation)
              for name in ('root', 'DEF-foot.L', 'DEF-foot.R', 'DEF-hand.L', 'DEF-hand.R', 'DEF-spine.006')}
    mesh_bounds = {}
    for obj in meshes:
        mesh_obj = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
        evaluated_mesh = mesh_obj.to_mesh()
        coords = [mesh_obj.matrix_world @ vertex.co for vertex in evaluated_mesh.vertices]
        assert all(math.isfinite(c) for vector in coords for c in vector), obj.name
        mesh_bounds[obj.name] = {'vertices': len(coords),
            'min': [min(v[i] for v in coords) for i in range(3)],
            'max': [max(v[i] for v in coords) for i in range(3)]}
        mesh_obj.to_mesh_clear()
    return {'frame': frame, 'bone_positions_metres': points,
        'head_rotation_quaternion': list(evaluated.pose.bones['DEF-spine.006'].matrix.to_quaternion()),
        'mesh_bounds_metres': mesh_bounds}

settings = dict(use_selection=True, object_types={'MESH', 'ARMATURE'}, global_scale=1.0,
    apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS', axis_forward='-Y', axis_up='Z',
    use_mesh_modifiers=False, mesh_smooth_type='FACE', add_leaf_bones=False,
    use_armature_deform_only=True, armature_nodetype='NULL', bake_anim=True,
    bake_anim_use_all_bones=True, bake_anim_use_nla_strips=False, bake_anim_use_all_actions=False,
    bake_anim_force_startend_keying=True, bake_anim_step=1, bake_anim_simplify_factor=0,
    path_mode='AUTO', embed_textures=False)
reports = []
for clip in ('Idle', 'TestArms', 'TestHead'):
    end = 181 if clip == 'Idle' else 121
    scene.frame_start, scene.frame_end = 1, end
    action = bpy.data.actions.new('Gratia_' + clip)
    rig.animation_data.action = action
    frames = [1, 46, 91, 136, 181] if clip == 'Idle' else [1, 61, 121]
    for frame in frames:
        scene.frame_set(frame)
        phase = (frame - 1) / (end - 1)
        if clip == 'Idle':
            wave = math.sin(phase * 2 * math.pi)
            pose(arm_extra=0.5 * wave, head_yaw=2 * wave, head_tilt=0.5 * wave, chest=0.5 * wave)
        elif clip == 'TestArms':
            amount = 1 if frame == 61 else 0
            pose(arm_extra=-43 * amount, elbow_extra=30 * amount)
        else:
            amount = 1 if frame == 61 else 0
            pose(head_yaw=18 * amount, head_tilt=4 * amount, chest=2 * amount)
        for name in animated:
            rig.pose.bones[name].keyframe_insert(data_path='rotation_quaternion', frame=frame, group=name)
    samples = [probe(frame) for frame in frames]
    for name in ('root', 'DEF-foot.L', 'DEF-foot.R'):
        start = Vector(samples[0]['bone_positions_metres'][name])
        assert all((Vector(s['bone_positions_metres'][name]) - start).length < 0.001 for s in samples), (clip, name)
    scene.frame_set(1)
    file = OUT / ('Gratia_' + clip + '.fbx')
    assert 'FINISHED' in bpy.ops.export_scene.fbx(filepath=str(file), **settings)
    reports.append({'clip': clip, 'fbx': str(file), 'duration_seconds': (end - 1) / 30,
        'frame_count': end, 'samples': samples})
    print('GRATIA_ANIMATION_EXPORTED ' + clip, flush=True)
(ROOT / 'evidence/02/animation_export_manifest.json').write_text(json.dumps({
    'source': bpy.data.filepath, 'source_saved': False, 'fps': 30,
    'method': 'Animate FK controls with arm IK_FK=1; bake evaluated rig. Preserve skeleton/reference pose.',
    'clips': reports}, indent=2), encoding='utf-8')
print('GRATIA_ANIMATION_EXPORT_DONE')
