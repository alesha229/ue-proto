"""Prepare an isolated 245-bone candidate in the live Blender MCP work copy.

Execute only through Blender MCP; no source .blend is saved. The original Rigify
continues to evaluate controls. Candidate transforms are derived from evaluated
armature-space matrices, then converted for its reduced parent hierarchy.
"""
import bpy, json, math
from pathlib import Path
from mathutils import Quaternion, Vector

ROOT = Path('E:/coding/ue proto')
OUT = ROOT / 'Exports/Gratia/GameRig'
OUT.mkdir(parents=True, exist_ok=True)
assert Path(bpy.data.filepath).name == 'Gratia_mvp.blend', bpy.data.filepath
source = bpy.data.objects['Gratia']
previous = bpy.data.collections.get('Gratia_GameRig_Candidate')
if previous:
    assert all(o.name == 'Gratia_GameRig' or o.name.endswith('_GameRig') for o in previous.objects)
    for obj in list(previous.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    bpy.data.collections.remove(previous)
mesh_names = [o.name for o in bpy.data.objects if o.type == 'MESH' and o.parent == source
              and not o.hide_render and o.name != 'Eyes horny']
source_meshes = [bpy.data.objects[name] for name in mesh_names]
face_keys = bpy.data.objects['Face'].data.shape_keys.key_blocks
if 'Mouth O' in face_keys:
    face_keys['Mouth O'].name = 'Mouth O wide'
for obj in source_meshes:
    obj.hide_set(False)
    obj.hide_viewport = False
    for modifier in obj.modifiers:
        modifier.show_viewport = modifier.type == 'ARMATURE'
    if obj.data.shape_keys:
        obj.data.shape_keys.animation_data_clear()
        for key in obj.data.shape_keys.key_blocks:
            key.value = 0

collection = bpy.data.collections.new('Gratia_GameRig_Candidate')
bpy.context.scene.collection.children.link(collection)
game_data = bpy.data.armatures.new('Gratia_GameRig')
game = bpy.data.objects.new('Gratia_GameRig', game_data)
collection.objects.link(game)
game.matrix_world = source.matrix_world.copy()
bpy.context.view_layer.update()
keep_names = {'root'} | {b.name for b in source.data.bones if b.use_deform}
parents = {}
for name in keep_names:
    ancestor = source.data.bones[name].parent
    seen = {name}
    while ancestor:
        candidate = ancestor.name
        if candidate in keep_names and candidate not in seen:
            break
        if candidate.startswith('ORG-'):
            analogue = 'DEF-' + candidate[4:]
            if analogue in keep_names and analogue not in seen:
                candidate = analogue
                break
        seen.add(ancestor.name)
        ancestor = ancestor.parent
    parents[name] = candidate if ancestor else None
    if name != 'root' and parents[name] is None:
        parents[name] = 'root'

for name in keep_names:
    seen = set()
    parent = name
    while parent is not None:
        assert parent not in seen, ('parent_cycle', name, parent)
        seen.add(parent)
        parent = parents[parent]

for obj in bpy.context.scene.objects:
    obj.select_set(False)
game.select_set(True)
bpy.context.view_layer.objects.active = game
with bpy.context.temp_override(object=game, active_object=game, selected_objects=[game], selected_editable_objects=[game]):
    bpy.ops.object.mode_set(mode='EDIT')
for name in sorted(keep_names):
    original = source.data.bones[name]
    bone = game_data.edit_bones.new(name)
    bone.head = original.head_local
    bone.tail = original.tail_local
    bone.matrix = original.matrix_local.copy()
    bone.length = original.length
    bone.use_deform = True
    bone.use_connect = False
for name, parent in parents.items():
    if parent:
        game_data.edit_bones[name].parent = game_data.edit_bones[parent]
with bpy.context.temp_override(object=game, active_object=game, selected_objects=[game], selected_editable_objects=[game]):
    bpy.ops.object.mode_set(mode='OBJECT')
bpy.context.view_layer.update()
rest_error = max(abs(b.matrix_local[r][c] - source.data.bones[b.name].matrix_local[r][c])
                 for b in game_data.bones for r in range(4) for c in range(4))
assert rest_error < 1e-4, ('rest_matrix_copy_error', rest_error)

copies = []
for obj in source_meshes:
    copy = obj.copy()
    copy.data = obj.data.copy()
    collection.objects.link(copy)
    copy.name = obj.name + '_GameRig'
    copy.parent = game
    copy.matrix_parent_inverse = obj.matrix_parent_inverse.copy()
    copy.matrix_basis = obj.matrix_basis.copy()
    copy.hide_render = False
    copy.hide_viewport = False
    copy.hide_set(False)
    for modifier in copy.modifiers:
        if modifier.type == 'ARMATURE':
            modifier.object = game
        modifier.show_viewport = modifier.type == 'ARMATURE'
    copies.append(copy)

for side in ('L','R'):
    source.pose.bones['upper_arm_parent.' + side]['IK_FK'] = 1.0
    source.update_tag()
animated = ['upper_arm_fk.L','upper_arm_fk.R','forearm_fk.L','forearm_fk.R','spine_fk.003','head']
for name in animated:
    source.pose.bones[name].rotation_mode = 'QUATERNION'
for bone in game.pose.bones:
    bone.rotation_mode = 'QUATERNION'

manifest = {'source_copy': bpy.data.filepath, 'source_rig_bones': len(source.data.bones),
    'candidate_bones': len(game.data.bones), 'parents': parents,
    'rest_matrix_max_error': rest_error,
    'meshes': [{'source':a.name,'candidate':b.name,'vertices':len(a.data.vertices),
                'preserve_volume': [m.use_deform_preserve_volume for m in a.modifiers if m.type=='ARMATURE']}
               for a,b in zip(source_meshes,copies)],
    'morph_aliases': {'Mouth O':'Mouth O wide'},
    'source_bbone_segments': {b.name:b.bbone_segments for b in source.data.bones if b.use_deform and b.bbone_segments>1},
    'candidate_linear_bbones': True, 'clips': [], 'runtime_approved': False}
(OUT / 'game_rig_manifest.json').write_text(json.dumps(manifest, indent=2),encoding='utf-8')
result = {'candidate':game.name,'bones':len(game.data.bones),'meshes':len(copies),
          'manifest':str(OUT / 'game_rig_manifest.json'), 'runtime_approved':False}
