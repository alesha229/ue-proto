"""Author short original reactions through Blender MCP using validated FK paths.

These are a soft head response and a moderate open-arm response, not a copied
video animation or a strong closed-fist gesture. Existing nine correctives are
reused, preserving the mesh's 59 working morphs.
"""
import bpy,json,math
import numpy as np
from pathlib import Path
from mathutils import Vector

ROOT=Path('E:/coding/ue proto')
OUT=ROOT/'Exports/Gratia/GameRig'
manifest=json.loads((OUT/'game_rig_manifest.json').read_text(encoding='utf-8'))
source,game=bpy.data.objects['Gratia'],bpy.data.objects['Gratia_GameRig']
scene=bpy.context.scene
source_actions=json.loads(game['mvp_source_actions'])
game_actions=json.loads(game['mvp_game_actions'])
pairs=[(bpy.data.objects[m['source']],bpy.data.objects[m['candidate']]) for m in manifest['meshes']]
animated=['upper_arm_fk.L','upper_arm_fk.R','forearm_fk.L','forearm_fk.R','spine_fk.003','head']
axis=source.data.bones['upper_arm_fk.L'].matrix_local.to_quaternion().inverted()@Vector((0,1,0))

def set_frame(frame):
    scene.frame_set(math.floor(frame),subframe=frame%1)
    bpy.context.view_layer.update()
    return bpy.context.evaluated_depsgraph_get()

def progress(clip):
    if clip=='TestArms':
        q=source.pose.bones['upper_arm_fk.L'].rotation_quaternion
        angle=math.degrees(2*math.atan2(Vector((q.x,q.y,q.z)).dot(axis),q.w))
        return max(0,min(1,(18-angle)/43))
    q=source.pose.bones['head'].rotation_quaternion
    return max(0,min(1,q.angle/math.radians(math.sqrt(18*18+4*4))))

def sample_time(clip,target):
    if target==0:return 1.0
    low,high=1.0,61.0
    for _ in range(20):
        mid=(low+high)/2
        set_frame(mid)
        if progress(clip)<target:low=mid
        else:high=mid
    return (low+high)/2

def envelope(frame,max_amount):
    t=(frame-1)/60
    if t<.4:
        v=t/.4
        return max_amount*(v*v*(3-2*v))
    if t<.6:return max_amount
    v=(1-t)/.4
    return max_amount*(v*v*(3-2*v))

def linear_keys(action):
    for layer in action.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                for curve in bag.fcurves:
                    for key in curve.keyframe_points:key.interpolation='LINEAR'

def positions(obj,dg):
    evaluated=obj.evaluated_get(dg)
    mesh=evaluated.to_mesh()
    co=np.empty(len(mesh.vertices)*3,dtype=np.float32)
    mesh.vertices.foreach_get('co',co)
    co=co.reshape((-1,3)).astype(np.float64)
    world=np.array(evaluated.matrix_world,dtype=np.float64)
    evaluated.to_mesh_clear()
    return co@world[:3,:3].T+world[:3,3]

def depth(bone):return 0 if not bone.parent else depth(bone.parent)+1
ordered=sorted(game.pose.bones,key=lambda b:depth(b.bone))
reports=[]
for name,pathway,maximum in (('ReactSoft','TestHead',.35),('ReactBright','TestArms',.55)):
    source.animation_data.action=bpy.data.actions[source_actions[pathway]]
    game.animation_data.action=bpy.data.actions[game_actions[pathway]]
    for original,candidate in pairs:
        candidate.data.shape_keys.animation_data.action=bpy.data.actions[candidate['mvp_corrective_action_'+pathway]]
    samples=[]
    for frame in range(1,62):
        source_frame=sample_time(pathway,envelope(frame,maximum))
        dg=set_frame(source_frame)
        evaluated=source.evaluated_get(dg)
        samples.append({'frame':frame,'source_frame':source_frame,
            'controls':{n:source.pose.bones[n].rotation_quaternion.copy() for n in animated},
            'matrices':{b.name:evaluated.pose.bones[b.name].matrix.copy() for b in ordered},
            'correctives':{k.name:k.value for k in pairs[0][1].data.shape_keys.key_blocks if k.name.startswith('Game_')}})

    source.animation_data.action=bpy.data.actions.new('MVP_Source_'+name)
    game.animation_data.action=bpy.data.actions.new('Gratia_Game_'+name)
    for original,candidate in pairs:
        candidate.data.shape_keys.animation_data.action=bpy.data.actions.new(name+'_Correctives_'+candidate.name)
    last_quat={}
    for sample in samples:
        frame=sample['frame']
        for control,value in sample['controls'].items():
            source.pose.bones[control].rotation_quaternion=value
            source.pose.bones[control].keyframe_insert('rotation_quaternion',frame=frame,group=control)
        for bone in ordered:
            parent=bone.parent
            args={'parent_matrix':sample['matrices'][parent.name],'parent_matrix_local':parent.bone.matrix_local} if parent else {}
            basis=bone.bone.convert_local_to_pose(sample['matrices'][bone.name],bone.bone.matrix_local,invert=True,**args)
            location,quat,scale=basis.decompose()
            if bone.name in last_quat and quat.dot(last_quat[bone.name])<0:quat.negate()
            last_quat[bone.name]=quat.copy()
            bone.location,bone.rotation_quaternion,bone.scale=location,quat,scale
            for channel in ('location','rotation_quaternion','scale'):bone.keyframe_insert(channel,frame=frame,group=bone.name)
        for original,candidate in pairs:
            for key in candidate.data.shape_keys.key_blocks:
                if key.name.startswith('Game_'):
                    key.value=sample['correctives'][key.name]
                    key.keyframe_insert('value',frame=frame,group='Correctives')
    linear_keys(source.animation_data.action)
    linear_keys(game.animation_data.action)
    for original,candidate in pairs:linear_keys(candidate.data.shape_keys.animation_data.action)
    scene.frame_start,scene.frame_end=1,61
    errors={a.name:0.0 for a,b in pairs}
    feet_ref=None;feet_drift=0.0;angle_error=0.0
    for frame in np.arange(1,61.1,.5):
        dg=set_frame(frame)
        a,b=source.evaluated_get(dg),game.evaluated_get(dg)
        feet={n:b.pose.bones[n].matrix.translation.copy() for n in ('root','DEF-foot.L','DEF-foot.R')}
        if feet_ref is None:feet_ref=feet
        feet_drift=max(feet_drift,max((feet[n]-feet_ref[n]).length for n in feet))
        for bone in ordered:
            angle=a.pose.bones[bone.name].matrix.to_quaternion().rotation_difference(b.pose.bones[bone.name].matrix.to_quaternion()).angle
            angle_error=max(angle_error,math.degrees(min(angle,abs(2*math.pi-angle))))
        for original,candidate in pairs:
            error=np.linalg.norm(positions(original,dg)-positions(candidate,dg),axis=1)
            assert np.isfinite(error).all(),original.name
            errors[original.name]=max(errors[original.name],float(error.max(initial=0)))
    start=set_frame(1)
    first={n:game.evaluated_get(start).pose.bones[n].matrix.copy() for n in ('root','DEF-foot.L','DEF-foot.R','DEF-hand.L','DEF-hand.R','DEF-spine.006')}
    last=set_frame(61)
    return_error=max((game.evaluated_get(last).pose.bones[n].matrix.translation-first[n].translation).length for n in first)
    set_frame(31)
    mid=game.evaluated_get(bpy.context.evaluated_depsgraph_get())
    midpoint={n:list(mid.pose.bones[n].matrix.translation) for n in ('DEF-hand.L','DEF-hand.R','DEF-spine.006')}
    set_frame(1)
    for obj in bpy.context.scene.objects:obj.select_set(False)
    selected=[game]+[b for a,b in pairs]
    for obj in selected:obj.select_set(True)
    bpy.context.view_layer.objects.active=game
    file=OUT/('Gratia_Game_'+name+'.fbx')
    with bpy.context.temp_override(active_object=game,object=game,selected_objects=selected,selected_editable_objects=selected):
        assert 'FINISHED' in bpy.ops.export_scene.fbx(filepath=str(file),use_selection=True,object_types={'MESH','ARMATURE'},
            global_scale=1,apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',
            use_mesh_modifiers=False,mesh_smooth_type='FACE',add_leaf_bones=False,use_armature_deform_only=True,
            armature_nodetype='NULL',path_mode='AUTO',embed_textures=False,bake_anim=True,bake_anim_use_all_bones=True,
            bake_anim_use_nla_strips=False,bake_anim_use_all_actions=False,bake_anim_force_startend_keying=True,
            bake_anim_step=1,bake_anim_simplify_factor=0)
    reports.append({'clip':name,'fbx':str(file),'duration_seconds':2,'frames':61,'samples':121,
        'source_pathway':pathway,'maximum_progress':maximum,'max_vertex_error_metres':errors,
        'max_bone_angle_error_degrees':angle_error,'root_foot_drift_metres':feet_drift,
        'neutral_return_position_error_metres':return_error,'midpoint_bone_positions_metres':midpoint,
        'passed':max(errors.values())<=.001 and angle_error<=.1 and feet_drift<=.001 and return_error<=.001,
        'visual_scope':'Soft head cue' if name=='ReactSoft' else 'Moderate open-arm cue; not a clenched-fist or hands-on-upper-costume pose'})
manifest['reaction_clips']=reports
(OUT/'game_rig_manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
(ROOT/'evidence/03/blender_mcp/reaction_export_manifest.json').write_text(json.dumps(reports,indent=2),encoding='utf-8')
result={'clips':reports}
