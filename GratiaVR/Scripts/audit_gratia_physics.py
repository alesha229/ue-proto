"""Read authored secondary-motion groups/settings through Blender MCP."""
import bpy,json,re,math
from pathlib import Path

ROOT=Path('E:/coding/ue proto')
rig=bpy.data.objects['Gratia']
game=bpy.data.objects['Gratia_GameRig']
old_audit=json.loads((ROOT/'evidence/03/blender_mcp/full_rig_material_audit.json').read_text(encoding='utf-8'))
weight_usage={}
for mesh,info in old_audit['meshes'].items():
    for name,count in info['weighted_groups'].items():
        weight_usage.setdefault(name,{})[mesh]=count

def scalar(value):
    if value is None or isinstance(value,(str,bool,int,float)):return value
    if isinstance(value,bpy.types.ID):return {'datablock':value.name,'type':type(value).__name__}
    try:return [scalar(x) for x in value]
    except TypeError:return str(value)

def rna(obj):
    result={}
    if obj is None:return None
    for prop in obj.bl_rna.properties:
        name=prop.identifier
        if name=='rna_type' or prop.type=='COLLECTION':continue
        try:
            value=getattr(obj,name)
            if prop.type=='POINTER':
                if isinstance(value,bpy.types.ID) or value is None:result[name]=scalar(value)
                continue
            result[name]=scalar(value)
        except Exception as error:result[name]={'unreadable':type(error).__name__}
    return result

def drivers(owner):
    if not owner.animation_data:return []
    result=[]
    for curve in owner.animation_data.drivers:
        d=curve.driver
        result.append({'path':curve.data_path,'index':curve.array_index,'valid':curve.is_valid,
            'type':d.type,'expression':d.expression,'use_self':d.use_self,
            'variables':[{'name':v.name,'type':v.type,'targets':[{
                'id':t.id.name if t.id else None,'data_path':t.data_path,'bone_target':t.bone_target,
                'transform_type':t.transform_type,'transform_space':t.transform_space,
                'rotation_mode':t.rotation_mode} for t in v.targets]} for v in d.variables]})
    return result

def category(name):
    low=name.lower()
    if 'hair' in low:return 'hair'
    if 'breast' in low:return 'body_breast_under_clothes'
    if low.startswith('def-ass.') or 'thigh' in low:return 'body_lower_under_clothes'
    if 'ear.' in low:return 'ears'
    if low=='def-tail':return 'tail_accessory'
    if 'tie' in low:return 'cloth_tie'
    if 'boot.' in low and 'decor' in low:return 'cloth_boot_decor'
    return None

groups={}
for bone in rig.data.bones:
    if not bone.use_deform:continue
    kind=category(bone.name)
    if kind is None:continue
    root=re.sub(r'\.\d+$','',bone.name)
    key=kind+':'+root
    groups.setdefault(key,{'kind':kind,'chain_name':root,'bones':[]})
    pose=rig.pose.bones[bone.name]
    parent=game.data.bones[bone.name].parent if bone.name in game.data.bones else None
    axis_y=(bone.tail_local-bone.head_local).normalized()
    basis=bone.matrix_local.to_3x3()
    relevant={bone.name,'ORG-'+bone.name[4:],bone.name[4:]}
    constraints=[]
    for related in relevant:
        pb=rig.pose.bones.get(related)
        if pb:
            constraints.extend({'owner':related,'settings':rna(c)} for c in pb.constraints)
    groups[key]['bones'].append({'blender_name':bone.name,'unreal_name':bone.name.replace('.','_'),
        'source_parent':bone.parent.name if bone.parent else None,
        'game_parent':parent.name if parent else None,
        'unreal_parent':parent.name.replace('.','_') if parent else None,
        'eligible_in_245_bone_candidate':bone.name in game.data.bones,
        'rest_length_metres':bone.length,'rest_head_metres':list(bone.head_local),
        'rest_tail_metres':list(bone.tail_local),'rest_matrix':[list(r) for r in bone.matrix_local],
        'blender_longitudinal_axis':'local Y','axis_y_armature_space':list(axis_y),
        'axis_x_armature_space':list(basis.col[0]),'axis_z_armature_space':list(basis.col[2]),
        'unreal_axis_policy':'Use imported bone rest quaternion; FBX bone axes are converted. Do not assume Blender Y equals UE X.',
        'weighted_vertices_by_mesh':weight_usage.get(bone.name,{}),'related_constraints':constraints,
        'custom_properties':{k:scalar(v) for k,v in pose.items()}})

helpers=[]
for obj in bpy.data.objects:
    if obj.name.endswith('_GameRig') or obj.name.startswith('WGT-'):continue
    low=obj.name.lower()
    has_sim=any(m.type in {'CLOTH','SOFT_BODY','COLLISION'} for m in obj.modifiers)
    if not has_sim and not any(x in low for x in ('phys','collision','collider')):continue
    mods=[]
    for m in obj.modifiers:
        item={'type':m.type,'settings':rna(m)}
        for sub in ('settings','collision_settings','effector_weights','point_cache'):
            if hasattr(m,sub):item[sub]=rna(getattr(m,sub))
        mods.append(item)
    helpers.append({'name':obj.name,'type':obj.type,'parent':obj.parent.name if obj.parent else None,
        'hidden_viewport':obj.hide_viewport,'hidden_render':obj.hide_render,'hide_set':obj.hide_get(),
        'vertices':len(obj.data.vertices) if obj.type=='MESH' else None,
        'purpose_from_bindings':{'weighted_visible_meshes':weight_usage.get(obj.name,{}),
             'modifiers_targeting_this':[{'owner':other.name,'modifier':m.name,'type':m.type}
                for other in bpy.data.objects for m in other.modifiers
                if any(getattr(m,key,None)==obj for key in ('object','target','mirror_object'))]},
        'modifiers':mods,'collision':rna(obj.collision) if obj.collision else None,
        'soft_body':rna(obj.soft_body) if obj.soft_body else None,
        'rigid_body':rna(obj.rigid_body) if obj.rigid_body else None,
        'constraints':[rna(c) for c in obj.constraints],'drivers':drivers(obj),
        'custom_properties':{k:scalar(v) for k,v in obj.items()}})

bone_groups=[]
for group in groups.values():
    names={b['blender_name'] for b in group['bones']}
    group['roots']=[b['blender_name'] for b in group['bones'] if b['game_parent'] not in names]
    group['total_length_metres']=sum(b['rest_length_metres'] for b in group['bones'])
    group['weighted_visible_meshes']=sorted({m for b in group['bones'] for m in b['weighted_vertices_by_mesh']})
    bone_groups.append(group)
report={'source_copy':bpy.data.filepath,'blender_version':bpy.app.version_string,
    'rig_bones':len(rig.data.bones),'candidate_bones':len(game.data.bones),
    'groups':bone_groups,'physics_helpers':helpers,'armature_drivers':drivers(rig),
    'scene_frame_range':{'start':bpy.context.scene.frame_start,'end':bpy.context.scene.frame_end,'fps':bpy.context.scene.render.fps},
    'no_direct_fbx_transfer':'Blender cloth, soft-body, drivers and constraints are not transferred as UE runtime physics. Values below are authored source settings, not claimed UE-equivalent parameters.',
    'clothing_shape_note':'Pants/Pants decor are driven by lower body DEF-ass/thigh groups and hidden AssPhys/ThighsPhys helpers; no independent DEF-skirt chain exists in this rig.',
    'unsupported_axis_conversion':'World/rest vectors are Blender armature coordinates in metres; obtain imported local bone axes in UE before constraints.',
    'source_autoexec_disabled':True}
out=ROOT/'evidence/03/blender_mcp/physics_group_manifest.json'
out.write_text(json.dumps(report,indent=2),encoding='utf-8')
(ROOT/'Exports/Gratia/GameRig/physics_group_manifest.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
result={'manifest':str(out),'groups':len(bone_groups),'secondary_bones':sum(len(g['bones']) for g in bone_groups),
        'all_secondary_bones_in_candidate':all(b['eligible_in_245_bone_candidate'] for g in bone_groups for b in g['bones']),
        'helpers':[{'name':h['name'],'modifiers':[m['type'] for m in h['modifiers']]} for h in helpers]}
