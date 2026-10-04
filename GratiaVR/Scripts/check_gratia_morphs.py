import bpy
import json
from pathlib import Path
out = Path(r'E:\coding\ue proto\evidence\02')
imported = json.loads((out / 'unreal_import_manifest.json').read_text(encoding='utf-8'))
face = bpy.data.objects['Face']
keys = face.data.shape_keys.key_blocks
aliases = json.loads((out / 'export_manifest.json').read_text(encoding='utf-8')).get('morph_aliases', {})
result = []
used_vertices = {index for polygon in face.data.polygons for index in polygon.vertices}
for key in keys:
    if key.name == 'Basis':
        continue
    delta = max((a.co-b.co).length for a,b in zip(key.data, keys['Basis'].data))
    changed = [i for i,(a,b) in enumerate(zip(key.data, keys['Basis'].data)) if (a.co-b.co).length > 0.000001]
    exported_name = aliases.get(key.name, key.name)
    result.append({'name': key.name, 'exported_name': exported_name, 'imported': exported_name in imported['morph_names'],
                   'max_delta_from_basis_metres': delta, 'relative_key': key.relative_key.name,
                   'vertex_group': key.vertex_group, 'changed_vertices': len(changed),
                   'changed_vertices_used_by_polygons': sum(i in used_vertices for i in changed)})
(out / 'morph_import_comparison.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print('GRATIA_MORPH_COMPARISON_DONE')
