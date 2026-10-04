"""Bake and validate one clean-rig clip through the live Blender MCP channel.

Set CLIP to Idle, TestArms, or TestHead before executing this source. Both the
original curved B-Bone skin and a separate linear FBX-equivalent reference are
compared; a discrepancy is recorded, never hidden by replacing the reference.
"""
import bpy, json, math
import numpy as np
from pathlib import Path
from mathutils import Quaternion, Vector

assert CLIP in ('Idle','TestArms','TestHead')
ROOT = Path('E:/coding/ue proto')
OUT = ROOT / 'Exports/Gratia/GameRig'
source = bpy.data.objects['Gratia']
game = bpy.data.objects['Gratia_GameRig']
scene = bpy.context.scene
scene.render.fps = 30
scene.render.fps_base = 1
manifest = json.loads((OUT / 'game_rig_manifest.json').read_text(encoding='utf-8'))
mesh_pairs = [(bpy.data.objects[m['source']],bpy.data.objects[m['candidate']]) for m in manifest['meshes']]
animated = ['upper_arm_fk.L','upper_arm_fk.R','forearm_fk.L','forearm_fk.R','spine_fk.003','head']
end = 181 if CLIP == 'Idle' else 121
scene.frame_start, scene.frame_end = 1, end
source.animation_data_create()
source.animation_data.action = bpy.data.actions.new('MVP_Source_' + CLIP)
game.animation_data_create()
game.animation_data.action = None

def rotation(name, axis, degrees):
    local_axis = source.pose.bones[name].bone.matrix_local.to_quaternion().inverted() @ Vector(axis)
    return Quaternion(local_axis, math.radians(degrees))

def pose(arm_extra=0,elbow_extra=0,head_yaw=0,head_tilt=0,chest=0):
    for side,sign in (('L',1),('R',-1)):
        upper, forearm = 'upper_arm_fk.'+side, 'forearm_fk.'+side
        source.pose.bones[upper].rotation_quaternion = rotation(upper,(0,1,0),sign*(18+arm_extra))
        source.pose.bones[forearm].rotation_quaternion = rotation(forearm,(1,0,0),-5-elbow_extra)
    source.pose.bones['spine_fk.003'].rotation_quaternion = rotation('spine_fk.003',(1,0,0),chest)
    source.pose.bones['head'].rotation_quaternion = rotation('head',(0,0,1),head_yaw) @ rotation('head',(1,0,0),head_tilt)

for frame in ([1,46,91,136,181] if CLIP=='Idle' else [1,61,121]):
    scene.frame_set(frame)
    wave = math.sin((frame-1)/(end-1)*2*math.pi)
    if CLIP=='Idle':
        pose(arm_extra=.5*wave,head_yaw=2*wave,head_tilt=.5*wave,chest=.5*wave)
    elif CLIP=='TestArms':
        amount = 1 if frame==61 else 0
        pose(arm_extra=-43*amount,elbow_extra=30*amount)
    else:
        amount = 1 if frame==61 else 0
        pose(head_yaw=18*amount,head_tilt=4*amount,chest=2*amount)
    for name in animated:
        source.pose.bones[name].keyframe_insert('rotation_quaternion',frame=frame,group=name)

def depth(bone):
    return 0 if not bone.parent else depth(bone.parent)+1

ordered = sorted(game.pose.bones,key=lambda b:depth(b.bone))
last_quat = {}
max_shear = 0.0
matrix_records = {}
def bake_frame(frame, key):
    global max_shear
    scene.frame_set(math.floor(frame),subframe=frame%1)
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    evaluated = source.evaluated_get(dg)
    to_game = game.matrix_world.inverted() @ source.matrix_world
    targets = {b.name:to_game @ evaluated.pose.bones[b.name].matrix.copy() for b in ordered}
    for bone in ordered:
        parent = bone.parent
        kwargs = {'parent_matrix':targets[parent.name], 'parent_matrix_local':parent.bone.matrix_local} if parent else {}
        basis = bone.bone.convert_local_to_pose(targets[bone.name],bone.bone.matrix_local,invert=True,**kwargs)
        location,quat,scale = basis.decompose()
        if bone.name in last_quat and quat.dot(last_quat[bone.name])<0:
            quat.negate()
        last_quat[bone.name] = quat.copy()
        from mathutils import Matrix
        recomposed = Matrix.LocRotScale(location,quat,scale)
        max_shear = max(max_shear,max(abs(basis[r][c]-recomposed[r][c]) for r in range(4) for c in range(4)))
        bone.location = location
        bone.rotation_quaternion = quat
        bone.scale = scale
        if key:
            for channel in ('location','rotation_quaternion','scale'):
                bone.keyframe_insert(channel,frame=frame,group=bone.name)
    bpy.context.view_layer.update()
    return targets

def positions(obj,dg):
    evaluated = obj.evaluated_get(dg)
    mesh = evaluated.to_mesh()
    coords = np.empty(len(mesh.vertices)*3,dtype=np.float32)
    mesh.vertices.foreach_get('co',coords)
    coords = coords.reshape((-1,3))
    world = np.array(evaluated.matrix_world,dtype=np.float64)
    points = coords @ world[:3,:3].T + world[:3,3]
    evaluated.to_mesh_clear()
    assert np.isfinite(points).all(), obj.name
    return points

def mesh_errors(dg):
    errors = {}
    for original,candidate in mesh_pairs:
        a,b = positions(original,dg),positions(candidate,dg)
        assert a.shape==b.shape,(original.name,a.shape,b.shape)
        error = np.linalg.norm(a-b,axis=1)
        errors[original.name] = float(error.max(initial=0))
    return errors

game.animation_data.action = bpy.data.actions.new('Gratia_Game_' + CLIP)
full_errors = {a.name:0.0 for a,b in mesh_pairs}
linear_errors = {a.name:0.0 for a,b in mesh_pairs}
max_angle_deg = 0.0
max_position = 0.0
foot_reference = None
foot_drift = 0.0
sample_count = 0
for frame in range(1,end+1):
    targets = bake_frame(frame,True)
    dg = bpy.context.evaluated_depsgraph_get()
    eval_game = game.evaluated_get(dg)
    for name,target in targets.items():
        current = eval_game.pose.bones[name].matrix
        max_position = max(max_position,(current.translation-target.translation).length)
        angle = current.to_quaternion().rotation_difference(target.to_quaternion()).angle
        angle = min(angle,abs(2*math.pi-angle))
        max_angle_deg = max(max_angle_deg,math.degrees(angle))
    feet = {name:targets[name].translation.copy() for name in ('root','DEF-foot.L','DEF-foot.R')}
    if foot_reference is None:
        foot_reference = feet
    foot_drift = max(foot_drift,max((feet[name]-foot_reference[name]).length for name in feet))
    for name,error in mesh_errors(dg).items():
        full_errors[name] = max(full_errors[name],error)
    sample_count+=1

# Dense linear interpolation avoids Blender's default Bezier overshoot between
# baked samples; source motion is checked again at half-frame times below.
action = game.animation_data.action
for layer in action.layers:
    for strip in layer.strips:
        for bag in strip.channelbags:
            for curve in bag.fcurves:
                for key in curve.keyframe_points:
                    key.interpolation = 'LINEAR'

intermediate_errors = {a.name:0.0 for a,b in mesh_pairs}
intermediate_angle_deg = 0.0
for frame in np.arange(1.5,end,.5)[::2]:
    scene.frame_set(math.floor(frame),subframe=frame%1)
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    eval_source,eval_game = source.evaluated_get(dg),game.evaluated_get(dg)
    for bone in game.pose.bones:
        a,b = eval_source.pose.bones[bone.name].matrix,eval_game.pose.bones[bone.name].matrix
        angle = a.to_quaternion().rotation_difference(b.to_quaternion()).angle
        angle = min(angle,abs(2*math.pi-angle))
        intermediate_angle_deg=max(intermediate_angle_deg,math.degrees(angle))
    for name,error in mesh_errors(dg).items():
        intermediate_errors[name] = max(intermediate_errors[name],error)
    sample_count+=1

# FBX has no Blender B-Bone curves: record that reference independently. This
# does not alter the full-source error above or approve a failing candidate.
segments = {b.name:b.bbone_segments for b in source.data.bones if b.bbone_segments>1}
for name in segments:
    source.data.bones[name].bbone_segments=1
for frame in (1,15.5,30.5,46,60.5,(end+1)/2,90.5,end):
    scene.frame_set(math.floor(frame),subframe=frame%1)
    bpy.context.view_layer.update()
    for name,error in mesh_errors(bpy.context.evaluated_depsgraph_get()).items():
        linear_errors[name]=max(linear_errors[name],error)
for name,count in segments.items():
    source.data.bones[name].bbone_segments=count

for obj in bpy.context.scene.objects:
    obj.select_set(False)
selected=[game]+[b for a,b in mesh_pairs]
for obj in selected:
    obj.select_set(True)
bpy.context.view_layer.objects.active=game
settings=dict(use_selection=True,object_types={'MESH','ARMATURE'},global_scale=1,
    apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',
    use_mesh_modifiers=False,mesh_smooth_type='FACE',add_leaf_bones=False,
    use_armature_deform_only=True,armature_nodetype='NULL',path_mode='AUTO',embed_textures=False,
    bake_anim=True,bake_anim_use_all_bones=True,bake_anim_use_nla_strips=False,
    bake_anim_use_all_actions=False,bake_anim_force_startend_keying=True,bake_anim_step=1,
    bake_anim_simplify_factor=0)
scene.frame_set(1)
file=OUT / ('Gratia_Game_'+CLIP+'.fbx')
with bpy.context.temp_override(active_object=game,object=game,selected_objects=selected,selected_editable_objects=selected):
    assert 'FINISHED' in bpy.ops.export_scene.fbx(filepath=str(file),**settings)
if CLIP=='Idle':
    mesh_file=OUT / 'Gratia_Game_preview.fbx'
    settings['bake_anim']=False
    with bpy.context.temp_override(active_object=game,object=game,selected_objects=selected,selected_editable_objects=selected):
        assert 'FINISHED' in bpy.ops.export_scene.fbx(filepath=str(mesh_file),**settings)

report={'clip':CLIP,'fbx':str(file),'frames':end,'duration_seconds':(end-1)/30,'sample_count':sample_count,
    'full_source_max_vertex_error_metres':full_errors,
    'intermediate_full_source_vertex_error_metres':intermediate_errors,
    'linear_fbx_reference_vertex_error_metres':linear_errors,
    'max_bone_position_error_metres':max_position,'max_bone_angle_error_degrees':max_angle_deg,
    'intermediate_max_bone_angle_error_degrees':intermediate_angle_deg,
    'max_trs_recomposition_error':max_shear,'max_root_foot_drift_metres':foot_drift,
    'passed_full_source':max(full_errors.values())<=.001 and max(intermediate_errors.values())<=.001 and max_angle_deg<=.1 and intermediate_angle_deg<=.1}
manifest['clips']=[c for c in manifest['clips'] if c['clip']!=CLIP]+[report]
manifest['runtime_approved']=len(manifest['clips'])==3 and all(c['passed_full_source'] for c in manifest['clips'])
(OUT / 'game_rig_manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
result=report
