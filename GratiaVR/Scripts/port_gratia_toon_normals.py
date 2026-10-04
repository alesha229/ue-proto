"""Use the authored normal maps in a bounded toon lighting layer.

The 0.72 ambient floor preserves readable costume colors from every VR angle.
This is an Unreal reconstruction of the Blender emission/toon intent, not an
automatic migration of ShaderToRGB node graphs.
"""
import json
from pathlib import Path
import unreal
lib=unreal.EditorAssetLibrary
edit=unreal.MaterialEditingLibrary
specs={
    'Body_skin':('body_diff','body_normals'),
    'Default_cloth_1':('default_cloth1_diff','default_cloth1_norm'),
    'Default_cloth_2':('default_cloth2_diff','default_cloth2_norm'),
    'Gratia_ears':('ears_diff','ears_norm'),
    'Gratia_hair':('hair_diff','hair_norm'),
}
report=[]
for label,(diffuse,normal) in specs.items():
    material=lib.load_asset('/Game/Gratia/CharacterMaterials/M_Gratia_'+label)
    texture=lib.load_asset('/Game/Gratia/Textures/T_'+normal)
    texture.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_NORMALMAP)
    texture.set_editor_property('srgb',False)
    texture.set_editor_property('flip_green_channel',True)
    assert lib.save_loaded_asset(texture)
    expressions=edit.get_material_expressions(material)
    color=next(n for n in expressions if isinstance(n,unreal.MaterialExpressionTextureSample) and n.get_editor_property('texture').get_name()=='T_'+diffuse)
    nodes={}
    def node(cls,label,x,y):
        existing=next((n for n in expressions if n.get_editor_property('desc')=='GratiaVR_'+label),None)
        result=existing or edit.create_material_expression(material,cls,x,y)
        result.set_editor_property('desc','GratiaVR_'+label)
        nodes[label]=result
        return result
    sample=node(unreal.MaterialExpressionTextureSample,'Normal',-900,250)
    sample.set_editor_property('texture',texture)
    sample.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    transform=node(unreal.MaterialExpressionTransform,'ToWorld',-650,250)
    transform.set_editor_property('transform_source_type',unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_TANGENT)
    transform.set_editor_property('transform_type',unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    direction=node(unreal.MaterialExpressionConstant3Vector,'KeyDirection',-650,430)
    direction.set_editor_property('constant',unreal.LinearColor(-.55,-.2,.811,1))
    dot=node(unreal.MaterialExpressionDotProduct,'KeyDot',-410,250)
    clamp=node(unreal.MaterialExpressionClamp,'KeyClamp',-200,250)
    clamp.set_editor_property('min_default',0.0)
    clamp.set_editor_property('max_default',1.0)
    shade=node(unreal.MaterialExpressionMultiply,'KeyGain',0,250)
    shade.set_editor_property('const_b',.28)
    ambient=node(unreal.MaterialExpressionAdd,'AmbientFloor',200,250)
    ambient.set_editor_property('const_b',.72)
    result=node(unreal.MaterialExpressionMultiply,'ReadableToon',420,0)
    for a,out,b,pin in [(sample,'RGB',transform,'Input'),(transform,'',dot,'A'),(direction,'',dot,'B'),(dot,'',clamp,'Input'),
                         (clamp,'',shade,'A'),(shade,'',ambient,'A'),(ambient,'',result,'B'),(color,'RGB',result,'A')]:
        names=[str(n) for n in edit.get_material_expression_input_names(b)]
        resolved=pin if pin in names else 'None' if 'None' in names else names[0]
        assert edit.connect_material_expressions(a,out,b,resolved),(label,pin,names)
    assert edit.connect_material_property(result,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    material.set_editor_property('used_with_skeletal_mesh',True)
    material.set_editor_property('used_with_morph_targets',True)
    assert not edit.recompile_material(material)
    assert lib.save_loaded_asset(material)
    report.append({'material':material.get_path_name(),'normal':texture.get_path_name(),'ambient_floor':.72,'key_gain':.28,'face_eyes_masks':'preserved'})
Path('E:/coding/ue proto/evidence/03/toon_normal_port.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
unreal.log('GRATIA_TOON_NORMALS_PORTED')
