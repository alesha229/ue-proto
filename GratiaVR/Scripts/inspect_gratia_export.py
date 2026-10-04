"""Read a .blend in a disposable Blender process; never save the source."""
import bpy
import json
from pathlib import Path

out = Path(r'E:\coding\ue proto\evidence\02')
out.mkdir(parents=True, exist_ok=True)
report = {'objects': [], 'materials': []}
for obj in bpy.data.objects:
    if obj.type not in {'MESH', 'ARMATURE'} or obj.name.startswith('WGT-'):
        continue
    report['objects'].append({
        'name': obj.name, 'type': obj.type, 'parent': obj.parent.name if obj.parent else None,
        'hidden_render': obj.hide_render, 'hidden_viewport': obj.hide_viewport, 'hidden': obj.hide_get(),
        'location': list(obj.location), 'scale': list(obj.scale),
        'materials': [slot.material.name if slot.material else None for slot in obj.material_slots],
        'modifiers': [{'name': m.name, 'type': m.type, 'visible': m.show_viewport,
                       'armature': m.object.name if m.type == 'ARMATURE' and m.object else None} for m in obj.modifiers],
        'vertices': len(obj.data.vertices) if obj.type == 'MESH' else None,
        'shape_keys': [k.name for k in obj.data.shape_keys.key_blocks] if obj.type == 'MESH' and obj.data.shape_keys else [],
    })
for mat in bpy.data.materials:
    report['materials'].append({'name': mat.name,
        'nodes': [{'name': n.name, 'type': n.type, 'image': n.image.name if n.type == 'TEX_IMAGE' and n.image else None,
                   'image_file': n.image.filepath if n.type == 'TEX_IMAGE' and n.image else None} for n in mat.node_tree.nodes] if mat.use_nodes else [],
        'links': [{'from': l.from_node.name, 'from_socket': l.from_socket.name,
                   'to': l.to_node.name, 'to_socket': l.to_socket.name} for l in mat.node_tree.links] if mat.use_nodes else [],
    })
(out / 'blender_export_inspection.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print('GRATIA_EXPORT_INSPECTION_DONE')
