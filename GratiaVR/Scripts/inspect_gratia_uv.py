import bpy
import json
from pathlib import Path
report = {}
for obj in bpy.data.objects:
    if obj.type == 'MESH' and obj.parent and obj.parent.name == 'Gratia' and not obj.hide_render:
        report[obj.name] = {'uv_layers': [{'name': uv.name, 'active_render': uv.active_render} for uv in obj.data.uv_layers],
            'active_index': obj.data.uv_layers.active_index,
            'materials': [s.material.name if s.material else None for s in obj.material_slots]}
for name in ['Gratia_face', 'Gratia_brows', 'Gratia_eyes', 'Gratia_eye_detail', 'Gratia_eyeshadows']:
    mat = bpy.data.materials[name]
    report[name] = {'uv_nodes': [{'name': n.name, 'type': n.type, 'uv_map': n.uv_map if n.type == 'UVMAP' else None}
                               for n in mat.node_tree.nodes if n.type in {'UVMAP','TEX_COORD','MAPPING'}]}
Path(r'E:\coding\ue proto\evidence\02\uv_inspection.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
