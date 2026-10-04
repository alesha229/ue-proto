"""Validate and export one corrected LBS game-rig clip over all/half frames."""
import bpy,json,math
import numpy as np
from pathlib import Path

ROOT=Path('E:/coding/ue proto')
OUT=ROOT/'Exports/Gratia/GameRig'
manifest=json.loads((OUT/'game_rig_manifest.json').read_text(encoding='utf-8'))
source,game=bpy.data.objects['Gratia'],bpy.data.objects['Gratia_GameRig']
scene=bpy.context.scene
assert CLIP in ('Idle','TestArms','TestHead')
source.animation_data.action=bpy.data.actions[json.loads(game['mvp_source_actions'])[CLIP]]
game.animation_data.action=bpy.data.actions[json.loads(game['mvp_game_actions'])[CLIP]]
pairs=[(bpy.data.objects[m['source']],bpy.data.objects[m['candidate']]) for m in manifest['meshes']]
for original,candidate in pairs:
    candidate.data.shape_keys.animation_data.action=bpy.data.actions[candidate['mvp_corrective_action_'+CLIP]]
end=181 if CLIP=='Idle' else 121
scene.frame_start,scene.frame_end=1,end

def positions(obj,dg):
    evaluated=obj.evaluated_get(dg)
    mesh=evaluated.to_mesh()
    co=np.empty(len(mesh.vertices)*3,dtype=np.float32)
    mesh.vertices.foreach_get('co',co)
    co=co.reshape((-1,3)).astype(np.float64)
    world=np.array(evaluated.matrix_world,dtype=np.float64)
    points=co@world[:3,:3].T+world[:3,3]
    evaluated.to_mesh_clear()
    assert np.isfinite(points).all(),obj.name
    return points

maximum={a.name:0.0 for a,b in pairs}
worst_sample={a.name:None for a,b in pairs}
max_angle=0.0
max_position=0.0
foot_ref=None
foot_drift=0.0
for frame in np.arange(1,end+.1,.5):
    scene.frame_set(math.floor(frame),subframe=frame%1)
    bpy.context.view_layer.update()
    dg=bpy.context.evaluated_depsgraph_get()
    a,b=source.evaluated_get(dg),game.evaluated_get(dg)
    for bone in game.pose.bones:
        original,candidate=a.pose.bones[bone.name].matrix,b.pose.bones[bone.name].matrix
        max_position=max(max_position,(original.translation-candidate.translation).length)
        angle=original.to_quaternion().rotation_difference(candidate.to_quaternion()).angle
        max_angle=max(max_angle,math.degrees(min(angle,abs(2*math.pi-angle))))
    feet={name:b.pose.bones[name].matrix.translation.copy() for name in ('root','DEF-foot.L','DEF-foot.R')}
    if foot_ref is None:foot_ref=feet
    foot_drift=max(foot_drift,max((feet[name]-foot_ref[name]).length for name in feet))
    for original,candidate in pairs:
        error=np.linalg.norm(positions(original,dg)-positions(candidate,dg),axis=1)
        value=float(error.max(initial=0))
        if value>maximum[original.name]:
            maximum[original.name]=value
            worst_sample[original.name]={'frame':float(frame),'vertex':int(error.argmax())}

scene.frame_set(1)
for obj in bpy.context.scene.objects:obj.select_set(False)
selected=[game]+[b for a,b in pairs]
for obj in selected:obj.select_set(True)
bpy.context.view_layer.objects.active=game
settings=dict(use_selection=True,object_types={'MESH','ARMATURE'},global_scale=1,
    apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',
    use_mesh_modifiers=False,mesh_smooth_type='FACE',add_leaf_bones=False,
    use_armature_deform_only=True,armature_nodetype='NULL',path_mode='AUTO',embed_textures=False,
    bake_anim=True,bake_anim_use_all_bones=True,bake_anim_use_nla_strips=False,
    bake_anim_use_all_actions=False,bake_anim_force_startend_keying=True,bake_anim_step=1,
    bake_anim_simplify_factor=0)
file=OUT/('Gratia_Game_'+CLIP+'.fbx')
with bpy.context.temp_override(active_object=game,object=game,selected_objects=selected,selected_editable_objects=selected):
    assert 'FINISHED' in bpy.ops.export_scene.fbx(filepath=str(file),**settings)
if CLIP=='Idle':
    settings['bake_anim']=False
    with bpy.context.temp_override(active_object=game,object=game,selected_objects=selected,selected_editable_objects=selected):
        assert 'FINISHED' in bpy.ops.export_scene.fbx(filepath=str(OUT/'Gratia_Game_preview.fbx'),**settings)

report={'clip':CLIP,'fbx':str(file),'frames':end,'samples':end*2-1,'skin':'Linear blend, preserve volume false',
    'full_source_max_vertex_error_metres':maximum,'worst_samples':worst_sample,
    'max_bone_angle_error_degrees':max_angle,'max_bone_position_error_metres':max_position,
    'max_root_foot_drift_metres':foot_drift,'passed_full_source':max(maximum.values())<=.001 and max_angle<=.1}
manifest['corrected_clips']=[c for c in manifest.get('corrected_clips',[]) if c['clip']!=CLIP]+[report]
manifest['corrective_basis']['validated']=len(manifest['corrected_clips'])==3 and all(c['passed_full_source'] for c in manifest['corrected_clips'])
manifest['runtime_approved']=manifest['corrective_basis']['validated']
manifest['approval_scope']='Three authored clips, all integer and half frames. Other poses and combined expressions require their own validation.'
(OUT/'game_rig_manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
result=report
