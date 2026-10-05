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
                   'internal_tension_stiffness', 'internal_compression_stiffness',
                   'internal_spring_max_length', 'air_damping']
collision_fields = ['use_collision', 'use_self_collision', 'distance_min', 'self_distance_min',
                    'collision_quality', 'self_friction']

def layer_enabled(obj):
    """An object whose every collection is excluded does not take part in simulation."""
    def find(layer, name):
        if layer.collection.name == name:
            return layer
        for child in layer.children:
            hit = find(child, name)
            if hit:
                return hit
        return None
    root = bpy.context.view_layer.layer_collection
    return any((l := find(root, c.name)) and not l.exclude for c in obj.users_collection)

def bone_weights(obj):
    weights = []
    for vertex in obj.data.vertices:
        influences = [(bone_name(obj.vertex_groups[g.group].name), g.weight) for g in vertex.groups]
        influences = [(n, w) for n, w in influences if n and w > 0.0]
        influences.sort(key=lambda v: v[1], reverse=True)
        influences = influences[:12]
        total = sum(w for n, w in influences)
        weights.append([dict(bone=n, weight=w / total) for n, w in influences] if total > 0 else [])
    return weights

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
                      gravity=cloth.settings.effector_weights.gravity,
                      collision={k: getattr(cloth.collision_settings, k) for k in collision_fields},
                      enabled_in_source_scene=layer_enabled(obj) and cloth.show_render,
                      consumers=consumers, enabled_in_source_render=any(c['render'] for c in consumers)))
# Collision objects that the enabled cloth cages collide with in the source scene.
colliders = []
for obj in bpy.data.objects:
    modifier = next((m for m in obj.modifiers if m.type == 'COLLISION'), None)
    if obj.type != 'MESH' or not modifier or not layer_enabled(obj) or not modifier.show_render:
        continue
    obj.data.calc_loop_triangles()
    weights = bone_weights(obj)
    assert all(weights), obj.name
    settings = obj.collision
    colliders.append(dict(name=obj.name, vertices=[position(obj, v) for v in obj.data.vertices],
                          triangles=[list(t.vertices) for t in obj.data.loop_triangles], weights=weights,
                          thickness_outer=settings.thickness_outer, thickness_inner=settings.thickness_inner,
                          friction=settings.cloth_friction, damping=settings.damping))
report = dict(schema=2, colliders=colliders, source=bpy.data.filepath, blender_version=bpy.app.version_string,
              coordinate_space='source armature rest space, metres; calibrate using shared bone heads',
              cages=cages, material_by_object=materials,
              bone_heads={b.name.replace('.', '_'):list(b.head_local) for b in game.data.bones})
destination = ROOT / 'Exports/Gratia/GameRig/source_cloth_cages.json'
destination.write_text(json.dumps(report, separators=(',', ':')), encoding='utf-8')
result = dict(path=str(destination), bytes=destination.stat().st_size,
              cages=[dict(name=c['name'], vertices=len(c['vertices']), triangles=len(c['triangles']),
                          dynamic=sum(p < 0.99 for p in c['pin']), source_enabled=c['enabled_in_source_render'],
                          scene_enabled=c['enabled_in_source_scene'], gravity=c['gravity'], collision=c['collision']) for c in cages],
              colliders=[dict(name=c['name'], vertices=len(c['vertices']), friction=c['friction']) for c in colliders])
