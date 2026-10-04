"""Export a neutral rigged preview from a disposable Blender session. Never saves .blend."""
import bpy
import json
from pathlib import Path

root = Path(r'E:\coding\ue proto')
out = root / 'Exports/Gratia'
out.mkdir(parents=True, exist_ok=True)
rig = bpy.data.objects.get('Gratia')
assert rig and rig.type == 'ARMATURE'
morph_aliases = {'Mouth O': 'Mouth O wide'}
# Unreal FName is case-insensitive: Mouth o and Mouth O otherwise collide.
for old_name, alias in morph_aliases.items():
    bpy.data.objects['Face'].data.shape_keys.key_blocks[old_name].name = alias
meshes = [o for o in bpy.data.objects if o.type == 'MESH' and o.parent == rig
          and not o.hide_render and o.name != 'Eyes horny']
assert {'Body', 'Face', 'Hair', 'Top', 'Pants', 'Boots'}.issubset({o.name for o in meshes})
for obj in bpy.context.scene.objects:
    obj.select_set(False)
for obj in [rig] + meshes:
    obj.hide_set(False)
    obj.hide_viewport = False
    obj.select_set(True)
    if obj.type == 'MESH' and obj.data.shape_keys:
        obj.data.shape_keys.animation_data_clear()
        for key in obj.data.shape_keys.key_blocks:
            key.value = 0
bpy.context.view_layer.objects.active = rig
settings = dict(filepath=str(out / 'Gratia_preview.fbx'), use_selection=True,
    object_types={'MESH', 'ARMATURE'}, global_scale=1.0, apply_unit_scale=True,
    apply_scale_options='FBX_SCALE_UNITS', axis_forward='-Y', axis_up='Z',
    use_mesh_modifiers=False, mesh_smooth_type='FACE', add_leaf_bones=False,
    use_armature_deform_only=True, armature_nodetype='NULL', bake_anim=False,
    path_mode='AUTO', embed_textures=False)
result = bpy.ops.export_scene.fbx(**settings)
assert 'FINISHED' in result
report = {'source': bpy.data.filepath, 'source_saved': False, 'fbx': settings['filepath'],
    'morph_aliases': morph_aliases,
    'settings': {k: sorted(v) if isinstance(v, set) else v for k,v in settings.items()},
    'selected_meshes': [o.name for o in meshes], 'base_vertices': sum(len(o.data.vertices) for o in meshes),
    'morphs': {o.name: [k.name for k in o.data.shape_keys.key_blocks if k.name != 'Basis']
               for o in meshes if o.data.shape_keys},
    'excluded': 'hidden physics helpers, control widgets, metarig, standalone sword and optional eye overlay',
    'material_conversion': 'Blender node shaders will be rebuilt as a first Unreal preview; visual matching remains open',
}
(root / 'evidence/02/export_manifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print('GRATIA_FBX_EXPORT_DONE')
