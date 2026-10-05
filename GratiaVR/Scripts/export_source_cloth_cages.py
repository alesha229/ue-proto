"""Read source cloth geometry/masks in the live MVP copy; execute through Blender MCP."""
import bpy
import json
from pathlib import Path

ROOT = Path('E:/coding/ue proto')
assert Path(bpy.data.filepath).resolve() == (ROOT / 'Exports/Gratia/Gratia_mvp.blend').resolve()
rig = bpy.data.objects['Gratia']
game = bpy.data.objects['Gratia_GameRig']
settings_fields = ['mass', 'quality', 'tension_stiffness', 'compression_stiffness',
                   'shear_stiffness', 'bending_stiffness', 'tension_damping',
                   'compression_damping', 'shear_damping', 'bending_damping',
                   'vertex_group_mass', 'pin_stiffness', 'use_pressure',
                   'uniform_pressure_force', 'use_internal_springs',
                   'internal_tension_stiffness', 'internal_compression_stiffness']

def position(obj, vertex):
    return list(rig.matrix_world.inverted() @ obj.matrix_world @ vertex.co)

def group_weight(obj, vertex, name):
    group = obj.vertex_groups.get(name)
    return next((g.weight for g in vertex.groups if group and g.group == group.index), 0.0)

def bone_name(name):
    if name in game.data.bones:
        return name.replace('.', '_')
    analogue = 'DEF-' + name[4:] if name.startswith('ORG-') else name
    return analogue.replace('.', '_') if analogue in game.data.bones else None

cages = []
materials = {}
for name in ['TitsPhys', 'AssPhys', 'ThighsPhys', 'Tail main', 'Tail R']:
    obj = bpy.data.objects[name]
    cloth = next(m for m in obj.modifiers if m.type == 'CLOTH')
    obj.data.calc_loop_triangles()
    weights = []
    for vertex in obj.data.vertices:
        influences = [(bone_name(obj.vertex_groups[g.group].name), g.weight) for g in vertex.groups]
        influences = [(n, w) for n, w in influences if n and w > 0.0]
        influences.sort(key=lambda v: v[1], reverse=True)
        influences = influences[:12]
        total = sum(w for n, w in influences)
        assert total > 0, (name, vertex.index)
        weights.append([dict(bone=n, weight=w / total) for n, w in influences])
    consumers = []
    for visible in bpy.data.objects:
        if visible.type != 'MESH' or visible.name.endswith('_GameRig') or visible.name.startswith('WGT-'):
            continue
        for modifier in visible.modifiers:
            if modifier.type != 'SURFACE_DEFORM' or modifier.target != obj:
                continue
            slots = [m.name for m in visible.data.materials if m]
            materials[visible.name] = slots
            samples = [dict(position=position(visible, v), weight=group_weight(visible, v, modifier.vertex_group)
                            if modifier.vertex_group else 1.0) for v in visible.data.vertices]
            consumers.append(dict(object=visible.name, materials=slots,
                                  vertex_group=modifier.vertex_group, viewport=modifier.show_viewport,
                                  render=modifier.show_render, strength=modifier.strength,
                                  influenced_vertices=samples))
    cages.append(dict(name=name, vertices=[position(obj, v) for v in obj.data.vertices],
                      normals=[list(v.normal) for v in obj.data.vertices],
                      triangles=[list(t.vertices) for t in obj.data.loop_triangles],
                      pin=[group_weight(obj, v, cloth.settings.vertex_group_mass) for v in obj.data.vertices],
                      weights=weights, settings={k: getattr(cloth.settings, k) for k in settings_fields},
                      consumers=consumers, enabled_in_source_render=any(c['render'] for c in consumers)))
report = dict(schema=1, source=bpy.data.filepath, blender_version=bpy.app.version_string,
              coordinate_space='source armature rest space, metres; calibrate using shared bone heads',
              cages=cages, material_by_object=materials,
              bone_heads={b.name.replace('.', '_'):list(b.head_local) for b in game.data.bones})
destination = ROOT / 'Exports/Gratia/GameRig/source_cloth_cages.json'
destination.write_text(json.dumps(report, separators=(',', ':')), encoding='utf-8')
result = dict(path=str(destination), bytes=destination.stat().st_size,
              cages=[dict(name=c['name'], vertices=len(c['vertices']), triangles=len(c['triangles']),
                          dynamic=sum(p < 0.99 for p in c['pin']), source_enabled=c['enabled_in_source_render']) for c in cages])
