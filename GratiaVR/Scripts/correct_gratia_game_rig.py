"""Add a small rest-space corrective basis to the isolated clean-rig candidate.

Blender MCP supplies this source to the active Gratia_mvp.blend session. Source
Rigify, B-Bone and preserve-volume skin remain untouched. Candidate skin is LBS
as in Unreal; each delta is solved through its inverse weighted skin transform.
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
assert Path(bpy.data.filepath).name=='Gratia_mvp.blend'
pairs=[(bpy.data.objects[m['source']],bpy.data.objects[m['candidate']]) for m in manifest['meshes']]
for original,candidate in pairs:
    for modifier in candidate.modifiers:
        if modifier.type=='ARMATURE':
            modifier.use_deform_preserve_volume=False
    if not candidate.data.shape_keys:
        candidate.shape_key_add(name='Basis')
    for key in list(candidate.data.shape_keys.key_blocks):
        if key.name.startswith('Game_'):
            candidate.shape_key_remove(key)
    candidate.data.shape_keys.animation_data_clear()
    for key in candidate.data.shape_keys.key_blocks:
        key.value=0

def latest_action(prefix):
    actions=[a for a in bpy.data.actions if a.name==prefix or a.name.startswith(prefix+'.')]
    assert actions,prefix
    return max(actions,key=lambda a:int(a.name.rsplit('.',1)[1]) if a.name!=prefix else 0)

source_actions={clip:latest_action('MVP_Source_'+clip) for clip in ('Idle','TestArms','TestHead')}
game_actions={clip:latest_action('Gratia_Game_'+clip) for clip in source_actions}
def set_clip(clip):
    source.animation_data.action=source_actions[clip]
    game.animation_data.action=game_actions[clip]
    scene.frame_end=181 if clip=='Idle' else 121

def set_frame(frame):
    scene.frame_set(math.floor(frame),subframe=frame%1)
    bpy.context.view_layer.update()
    return bpy.context.evaluated_depsgraph_get()

axis=source.data.bones['upper_arm_fk.L'].matrix_local.to_quaternion().inverted() @ Vector((0,1,0))
def amount(clip):
    if clip=='TestArms':
        q=source.pose.bones['upper_arm_fk.L'].rotation_quaternion
        angle=math.degrees(2*math.atan2(Vector((q.x,q.y,q.z)).dot(axis),q.w))
        return max(0,min(1,(18-angle)/43))
    q=source.pose.bones['head'].rotation_quaternion
    # Source keyframes combine yaw and tilt linearly in the authored quaternion.
    # Relative progress along that geodesic is monotone on the first half.
    neutral=source_actions[clip]
    return max(0,min(1,q.angle / math.radians(math.sqrt(18*18+4*4))))

def sample_time(clip,target):
    if target==0:return 1.0
    if target==1:return 61.0
    low,high=1.0,61.0
    for _ in range(24):
        mid=(low+high)/2
        set_frame(mid)
        if amount(clip)<target:low=mid
        else:high=mid
    return (low+high)/2

def coords(obj,dg):
    evaluated=obj.evaluated_get(dg)
    mesh=evaluated.to_mesh()
    co=np.empty(len(mesh.vertices)*3,dtype=np.float32)
    mesh.vertices.foreach_get('co',co)
    result=co.reshape((-1,3)).astype(np.float64)
    evaluated.to_mesh_clear()
    return result

weighted_indices={}
for original,candidate in pairs:
    vertex_indices=[];bone_names=[];weights=[]
    for vertex in candidate.data.vertices:
        for group in vertex.groups:
            name=candidate.vertex_groups[group.group].name
            if name in game.data.bones and group.weight>1e-8:
                vertex_indices.append(vertex.index);bone_names.append(name);weights.append(group.weight)
    weighted_indices[candidate.name]=(np.array(vertex_indices),bone_names,np.array(weights))

def inverse_delta(candidate,difference,dg):
    rig=game.evaluated_get(dg)
    vi,names,weights=weighted_indices[candidate.name]
    count=len(candidate.data.vertices)
    skin={name:np.array(rig.pose.bones[name].matrix @ game.data.bones[name].matrix_local.inverted(),dtype=np.float64)[:3,:3]
          for name in set(names)}
    matrices=np.zeros((count,3,3),dtype=np.float64)
    total=np.zeros(count,dtype=np.float64)
    blocks=np.array([skin[name] for name in names])*weights[:,None,None]
    np.add.at(matrices,vi,blocks)
    np.add.at(total,vi,weights)
    mask=total>1e-8
    matrices[mask]/=total[mask,None,None]
    matrices[~mask]=np.eye(3)
    return np.linalg.solve(matrices,difference[...,None])[...,0]

node_values=[0,.25,.5,.75,1]
records=[]
for clip in ('TestArms','TestHead'):
    set_clip(clip)
    for index,target in enumerate(node_values):
        if clip=='TestHead' and index==0:
            continue
        frame=sample_time(clip,target)
        dg=set_frame(frame)
        name='Game_NeutralCorrective' if index==0 else 'Game_'+('Arms' if clip=='TestArms' else 'Head')+'Corrective_'+str(index)
        maximum=0.0
        for original,candidate in pairs:
            for key in candidate.data.shape_keys.key_blocks:
                if key.name.startswith('Game_'):key.value=0
        dg=set_frame(frame)
        for original,candidate in pairs:
            target_co=coords(original,dg)
            linear_co=coords(candidate,dg)
            difference=target_co-linear_co
            delta=inverse_delta(candidate,difference,dg)
            basis=candidate.data.shape_keys.key_blocks['Basis']
            base=np.empty(len(basis.data)*3,dtype=np.float32)
            basis.data.foreach_get('co',base)
            key=candidate.shape_key_add(name=name,from_mix=False)
            key.data.foreach_set('co',(base.reshape((-1,3))+delta).astype(np.float32).ravel())
            key.value=0
            maximum=max(maximum,float(np.linalg.norm(delta,axis=1).max(initial=0)))
        records.append({'clip':clip,'amount':target,'frame':frame,'shape':name,'max_rest_delta_metres':maximum})

def apply_weights(clip):
    progress=0 if clip=='Idle' else amount(clip)
    index=min(3,int(progress*4))
    blend=progress*4-index
    names=['Game_NeutralCorrective']+['Game_'+('Arms' if clip=='TestArms' else 'Head')+'Corrective_'+str(i) for i in range(1,5)]
    values={names[index]:1-blend,names[index+1]:blend} if clip!='Idle' else {'Game_NeutralCorrective':1}
    for original,candidate in pairs:
        for key in candidate.data.shape_keys.key_blocks:
            if key.name.startswith('Game_'):
                key.value=values.get(key.name,0)
    return values

for clip in ('Idle','TestArms','TestHead'):
    set_clip(clip)
    end=181 if clip=='Idle' else 121
    for original,candidate in pairs:
        candidate.data.shape_keys.animation_data_create()
        candidate.data.shape_keys.animation_data.action=bpy.data.actions.new('Game_Correctives_'+clip+'_'+candidate.name)
    for frame in range(1,end+1):
        set_frame(frame)
        apply_weights(clip)
        for original,candidate in pairs:
            for key in candidate.data.shape_keys.key_blocks:
                if key.name.startswith('Game_'):
                    key.keyframe_insert('value',frame=frame,group='Correctives')
    for original,candidate in pairs:
        action=candidate.data.shape_keys.animation_data.action
        candidate['mvp_corrective_action_'+clip]=action.name
        for layer in action.layers:
            for strip in layer.strips:
                for bag in strip.channelbags:
                    for curve in bag.fcurves:
                        for key in curve.keyframe_points:key.interpolation='LINEAR'

game['mvp_source_actions']=json.dumps({k:a.name for k,a in source_actions.items()})
game['mvp_game_actions']=json.dumps({k:a.name for k,a in game_actions.items()})
manifest['corrective_basis']={'method':'Inverse normalized weighted LBS skin 3x3; source DQS and B-Bones retained.',
    'nodes':records,'basis_count':len(records),'candidate_preserve_volume':False,
    'original_50_working_morphs_preserved':True,
    'clip_weight_actions':'Per-mesh dense 30Hz linear shape curves', 'validated':False}
manifest['runtime_approved']=False
(OUT/'game_rig_manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
result=manifest['corrective_basis']
