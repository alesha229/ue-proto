"""Create cooked OpenXR bindings for continuous left-stick movement."""
import json
from pathlib import Path
import unreal

folder = '/Game/Gratia/Input'
tools = unreal.AssetToolsHelpers.get_asset_tools()
library = unreal.EditorAssetLibrary

def asset(name, cls):
    path = folder + '/' + name
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', cls)
    return library.load_asset(path) if library.does_asset_exist(path) else tools.create_asset(name, folder, cls, factory)

walk = asset('IA_Walk', unreal.InputAction)
turn = asset('IA_SnapTurn', unreal.InputAction)
block = asset('IA_BlockTeleport', unreal.InputAction)
context = asset('IMC_GratiaLocomotion', unreal.InputMappingContext)
for action, kind in [(walk, unreal.InputActionValueType.AXIS2D), (turn, unreal.InputActionValueType.AXIS1D), (block, unreal.InputActionValueType.AXIS1D)]:
    action.set_editor_property('value_type', kind)
    action.set_editor_property('consume_input', True)
walk.set_editor_property('accumulation_behavior', unreal.InputActionAccumulationBehavior.CUMULATIVE)
context.unmap_all()
bindings = []

def mapping(action, key, y=False, negative=False):
    input_key = unreal.Key()
    input_key.set_editor_property('key_name', key)
    result = context.map_key(action, input_key)
    modifiers = []
    if negative:
        negate = unreal.new_object(unreal.InputModifierNegate, outer=context)
        modifiers.append(negate)
    if y:
        swizzle = unreal.new_object(unreal.InputModifierSwizzleAxis, outer=context)
        swizzle.set_editor_property('order', unreal.InputAxisSwizzle.YXZ)
        modifiers.append(swizzle)
    result.set_editor_property('modifiers', modifiers)
    # map_key returns a struct copy; save the modified struct back to the context.
    data = context.get_editor_property('default_key_mappings')
    values = list(data.get_editor_property('mappings'))
    values[-1] = result
    data.set_editor_property('mappings', values)
    context.set_editor_property('default_key_mappings', data)
    bindings.append({'action': action.get_name(), 'key': key, 'y': y, 'negative': negative})

for prefix, stick in [('OculusTouch','Thumbstick'), ('ValveIndex','Thumbstick'), ('MixedReality','Thumbstick'), ('Vive','Trackpad'), ('PICO','Thumbstick')]:
    mapping(walk, f'{prefix}_Left_{stick}_X')
    mapping(walk, f'{prefix}_Left_{stick}_Y', y=True)
    mapping(turn, f'{prefix}_Right_{stick}_X')
    mapping(block, f'{prefix}_Right_{stick}_Y')
mapping(walk, 'Gamepad_LeftX')
mapping(walk, 'Gamepad_LeftY', y=True)
mapping(turn, 'Gamepad_RightX')
for key, y, negative in [('W',True,False), ('S',True,True), ('D',False,False), ('A',False,True)]:
    mapping(walk, key, y=y, negative=negative)
for key, negative in [('E',False), ('Q',True)]:
    mapping(turn, key, negative=negative)
toggle = asset('IA_MenuToggle', unreal.InputAction)
next_action = asset('IA_MenuNext', unreal.InputAction)
apply_action = asset('IA_MenuApply', unreal.InputAction)
toggle.set_editor_property('value_type', unreal.InputActionValueType.BOOLEAN)
next_action.set_editor_property('value_type', unreal.InputActionValueType.AXIS1D)
apply_action.set_editor_property('value_type', unreal.InputActionValueType.AXIS1D)
for action in [toggle, next_action, apply_action]:
    action.set_editor_property('consume_input', True)
template = library.load_asset('/Game/XRFramework/Input/IMC_Default')
for source_mapping in template.get_editor_property('default_key_mappings').get_editor_property('mappings'):
    if str(source_mapping.get_editor_property('action').get_name()).startswith('IA_Menu_Toggle'):
        key = str(source_mapping.get_editor_property('key').get_editor_property('key_name'))
        mapping(toggle, key)
mapping(toggle, 'F4')
movement_context = context
context = asset('IMC_GratiaMenu', unreal.InputMappingContext)
context.unmap_all()
for prefix in ['OculusTouch', 'ValveIndex']:
    mapping(next_action, prefix + '_Right_A_Click')
    mapping(next_action, prefix + '_Left_X_Click', negative=True)
    mapping(apply_action, prefix + '_Right_Trigger_Axis')
mapping(next_action, 'Down')
mapping(next_action, 'Up', negative=True)
mapping(apply_action, 'Enter')
for item in [walk, turn, block, toggle, next_action, apply_action, movement_context, context]:
    description = 'action_description' if isinstance(item, unreal.InputAction) else 'context_description'
    item.set_editor_property(description, item.get_name())
    assert library.save_loaded_asset(item, only_if_is_dirty=False)
Path(r'E:/coding/ue proto/evidence/02/locomotion_input_manifest.json').write_text(json.dumps({'priority':50, 'bindings':bindings}, indent=2), encoding='utf-8')
unreal.log('GRATIA_LOCOMOTION_INPUT_CREATED')
