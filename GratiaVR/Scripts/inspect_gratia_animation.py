"""Inspect pose controls and deform bones without saving the .blend."""
import bpy
import json
from pathlib import Path

rig = bpy.data.objects['Gratia']
report = {'source_saved': False, 'rig': rig.name, 'matrix_world': [list(r) for r in rig.matrix_world],
    'pose_position': rig.data.pose_position, 'bones': [],
    'properties': {b.name: {k: str(b[k]) for k in b.keys()} for b in rig.pose.bones if b.keys()}}
for bone in rig.pose.bones:
    if not bone.bone.use_deform and not any(word in bone.name.lower() for word in ('spine', 'upper_arm', 'forearm', 'hand', 'head', 'neck', 'shoulder', 'thigh', 'foot', 'shin')):
        continue
    report['bones'].append({'name': bone.name, 'deform': bone.bone.use_deform,
        'parent': bone.parent.name if bone.parent else None,
        'head': list(bone.bone.head_local), 'tail': list(bone.bone.tail_local),
        'rest_matrix': [list(row) for row in bone.bone.matrix_local],
        'basis': [list(row) for row in bone.matrix_basis],
        'constraints': [{'name': c.name, 'type': c.type, 'influence': c.influence} for c in bone.constraints]})
(Path(r'E:\coding\ue proto\evidence\02') / 'rig_animation_audit.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print('GRATIA_ANIMATION_AUDIT_DONE')
