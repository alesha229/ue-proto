"""Generate native names/roles from the Blender MCP physics audit."""
import json
from pathlib import Path

root=Path(__file__).resolve().parents[2]
audit=json.loads((root/'evidence/03/blender_mcp/physics_group_manifest.json').read_text(encoding='utf-8'))
codes={'hair':1,'cloth_tie':2,'cloth_boot_decor':2,'body_breast_under_clothes':3,
       'body_lower_under_clothes':3,'ears':4,'tail_accessory':4}
rows=[]
for group in audit['groups']:
    for bone in group['bones']:
        name=bone['unreal_name']
        safe=not name.startswith('DEF-thigh')
        rows.append((name,codes[group['kind']],bone['rest_length_metres']*100,safe))
assert len(rows)==150 and len({r[0] for r in rows})==150
text='''#pragma once

#include "CoreMinimal.h"

// Generated from evidence/03/blender_mcp/physics_group_manifest.json.
// Authored Blender cloth masses/stiffness do not map one-to-one to Chaos.
// Groups: 1 hair, 2 cloth/decor, 3 local body, 4 ears/tail accessory.
struct FGratiaSecondaryBoneDef
{
    const TCHAR* Name;
    uint8 Group;
    float SourceRestLengthCm;
    bool bSafeDefaultSimulation;
};

namespace GratiaSecondaryBones
{
inline const FGratiaSecondaryBoneDef Definitions[] =
{
'''
for name,code,length,safe in rows:
    text+=f'    {{ TEXT("{name}"), {code}, {length:.6f}f, {str(safe).lower()} }},\n'
text+='''};

inline const FGratiaSecondaryBoneDef* Find(FName BoneName)
{
    for (const FGratiaSecondaryBoneDef& Definition : Definitions)
    {
        if (BoneName == FName(Definition.Name))
        {
            return &Definition;
        }
    }
    return nullptr;
}
}
'''
target=root/'GratiaVR/Source/GratiaVREditorTools/GratiaSecondaryBones.h'
target.write_text(text,encoding='utf-8')
print(f'{target}: {len(rows)} source secondary roles; {sum(r[3] for r in rows)} safe default simulation roles')
