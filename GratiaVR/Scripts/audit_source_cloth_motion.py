"""Observe source cloth cages through live Blender MCP, restoring viewport state."""
import bpy
import json
from pathlib import Path

assert Path(bpy.data.filepath).name == 'Gratia_mvp.blend'
scene = bpy.context.scene
saved_frame = scene.frame_current
saved = [(m, m.show_viewport) for o in bpy.data.objects for m in o.modifiers
         if o.name in {'Body','Top','Pants','Boots','Pants decor'} and m.type == 'SURFACE_DEFORM']
names = ['TitsPhys','AssPhys','ThighsPhys']
try:
    for modifier, value in saved:
        modifier.show_viewport = modifier.show_render
    scene.frame_set(1)
    dependency = bpy.context.evaluated_depsgraph_get()
    before = {name: [v.co.copy() for v in bpy.data.objects[name].evaluated_get(dependency).data.vertices] for name in names}
    peak = {name:0.0 for name in names}
    for frame in range(2, 42):
        scene.frame_set(frame)
        dependency = bpy.context.evaluated_depsgraph_get()
        for name in names:
            points = bpy.data.objects[name].evaluated_get(dependency).data.vertices
            peak[name] = max(peak[name], max((v.co-before[name][i]).length for i,v in enumerate(points)))
    report = dict(source=bpy.data.filepath, frames=[1,41], unit='metres', max_vertex_motion=peak,
                  scope='Source scene animation, gravity/pressure and authored pins; no hand interaction claim')
    Path('E:/coding/ue proto/evidence/04/blender_cloth_motion_reference.json').write_text(json.dumps(report,indent=2))
    result = report
finally:
    for modifier, value in saved:
        modifier.show_viewport = value
    scene.frame_set(saved_frame)
