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
grab_left = asset('IA_GrabLeft', unreal.InputAction)
grab_right = asset('IA_GrabRight', unreal.InputAction)
# Grip squeezes the hand around soft parts (and also grabs); triggers keep the grab.
grip_left = asset('IA_GripLeft', unreal.InputAction)
grip_right = asset('IA_GripRight', unreal.InputAction)
for action in (grab_left, grab_right, grip_left, grip_right):
    action.set_editor_property('value_type', unreal.InputActionValueType.AXIS1D)
    action.set_editor_property('consume_input', True)
# Clicking either stick recenters the player (the menu item and the loading tips promise it).
recenter = asset('IA_Recenter', unreal.InputAction)
recenter.set_editor_property('value_type', unreal.InputActionValueType.BOOLEAN)
recenter.set_editor_property('consume_input', True)
bindings = []

def mapping(action, key, y=False, negative=False):
    input_key = unreal.Key()
    input_key.set_editor_property('key_name', key)
    assert unreal.InputLibrary.key_is_valid(input_key), 'Unregistered input key: ' + key
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

xr_sticks = [('OculusTouch','Thumbstick'), ('ValveIndex','Thumbstick'),
             ('MixedReality','Thumbstick'), ('Vive','Trackpad')]
for prefix, stick in xr_sticks:
    # OpenXR builds a VECTOR2F action from Axis2D. Its suggested path must be
    # the vector parent (/input/thumbstick), never the scalar /x and /y paths.
    # Swizzle belongs only to scalar desktop mappings, not the complete XR vector.
    mapping(walk, f'{prefix}_Left_{stick}_2D')
    mapping(turn, f'{prefix}_Right_{stick}_X')
    mapping(block, f'{prefix}_Right_{stick}_Y')
    mapping(grab_left, f'{prefix}_Left_Trigger_Axis')
    mapping(grab_right, f'{prefix}_Right_Trigger_Axis')
    for side, action in (('Left', grip_left), ('Right', grip_right)):
        # Analog grip where the controller has one, otherwise its grip button.
        for suffix in ('Grip_Axis', 'Grip_Click'):
            key = f'{prefix}_{side}_{suffix}'
            probe = unreal.Key()
            probe.set_editor_property('key_name', key)
            if unreal.InputLibrary.key_is_valid(probe):
                mapping(action, key)
                break
    # Sticks only: a Vive trackpad is pressed while walking.
    for side in ('Left', 'Right') if stick == 'Thumbstick' else ():
        mapping(recenter, f'{prefix}_{side}_{stick}_Click')
mapping(grab_left, 'Z')
mapping(grab_right, 'X')
mapping(grip_left, 'C')
mapping(grip_right, 'V')
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
template_mappings = list(template.get_editor_property('default_key_mappings').get_editor_property('mappings'))
template_action_names = sorted({value.get_editor_property('action').get_name()
                                for value in template_mappings if value.get_editor_property('action')})
unreal.log('GRATIA_TEMPLATE_INPUT_ACTIONS: ' + json.dumps(template_action_names))
removed_template_bindings = []
for source_mapping in template_mappings:
    source_action = source_mapping.get_editor_property('action')
    if not source_action:
        continue
    source_name = source_action.get_name()
    if source_name == 'IA_Move' or source_name == 'IA_Turn' or source_name.startswith('IA_Turn_'):
        input_key = source_mapping.get_editor_property('key')
        key = str(input_key.get_editor_property('key_name'))
        removed_template_bindings.append({'action': source_name, 'key': key})
        template.unmap_key(source_action, input_key)
assert library.save_loaded_asset(template, only_if_is_dirty=False)
skipped_menu_keys = []
for source_mapping in template_mappings:
    source_action = source_mapping.get_editor_property('action')
    if source_action and str(source_action.get_name()).startswith('IA_Menu_Toggle'):
        input_key = source_mapping.get_editor_property('key')
        key = str(input_key.get_editor_property('key_name'))
        if not unreal.InputLibrary.key_is_valid(input_key):
            skipped_menu_keys.append(key)
            continue
        mapping(toggle, key)
mapping(toggle, 'F4')
movement_context = context
context = asset('IMC_GratiaMenu', unreal.InputMappingContext)
context.unmap_all()
for prefix, left_previous in [('OculusTouch', 'X'), ('ValveIndex', 'A')]:
    mapping(next_action, prefix + '_Right_A_Click')
    # Index has A/B buttons on both hands, rather than Touch's left X/Y.
    mapping(next_action, prefix + f'_Left_{left_previous}_Click', negative=True)
    mapping(apply_action, prefix + '_Right_Trigger_Axis')
    # Either stick moves the menu focus (up = previous). In the asset, so OpenXR binds it.
    for side in ('Left', 'Right'):
        mapping(next_action, f'{prefix}_{side}_Thumbstick_Y', negative=True)
mapping(next_action, 'Down')
mapping(next_action, 'Up', negative=True)
mapping(apply_action, 'Enter')
for item in [walk, turn, block, grab_left, grab_right, grip_left, grip_right, recenter, toggle, next_action, apply_action, movement_context, context]:
    description = 'action_description' if isinstance(item, unreal.InputAction) else 'context_description'
    item.set_editor_property(description, item.get_name())
    assert library.save_loaded_asset(item, only_if_is_dirty=False)

def check_saved_bindings():
    """Inspect serialized mappings, not only the arguments used to create them."""
    saved = library.load_asset(movement_context.get_path_name())
    mappings = list(saved.get_editor_property('default_key_mappings').get_editor_property('mappings'))
    by_key = {}
    for value in mappings:
        key = str(value.get_editor_property('key').get_editor_property('key_name'))
        by_key[key] = value
    expected_xr_keys = {f'{prefix}_Left_{stick}_2D' for prefix, stick in xr_sticks}
    actual_xr_walk_keys = {
        str(value.get_editor_property('key').get_editor_property('key_name'))
        for value in mappings
        if value.get_editor_property('action') == walk
        and str(value.get_editor_property('key').get_editor_property('key_name')).split('_')[0]
        in {prefix for prefix, _ in xr_sticks}
    }
    assert actual_xr_walk_keys == expected_xr_keys, (actual_xr_walk_keys, expected_xr_keys)
    assert walk.get_editor_property('value_type') == unreal.InputActionValueType.AXIS2D
    for key in expected_xr_keys:
        assert not by_key[key].get_editor_property('modifiers'), f'{key} must preserve the complete XR vector'
    for key in ['W', 'S', 'Gamepad_LeftY']:
        modifiers = list(by_key[key].get_editor_property('modifiers'))
        swizzles = [value for value in modifiers if isinstance(value, unreal.InputModifierSwizzleAxis)]
        assert len(swizzles) == 1 and swizzles[0].get_editor_property('order') == unreal.InputAxisSwizzle.YXZ
    for key in ['S', 'A']:
        assert any(isinstance(value, unreal.InputModifierNegate) for value in by_key[key].get_editor_property('modifiers'))
    for prefix, stick in xr_sticks:
        key = f'{prefix}_Right_{stick}_Y'
        assert by_key[key].get_editor_property('action') == block, f'{key} must consume legacy teleport actions'
    remaining_template = template.get_editor_property('default_key_mappings').get_editor_property('mappings')
    for value in remaining_template:
        action = value.get_editor_property('action')
        if action:
            assert action.get_name() not in ['IA_Move', 'IA_Turn'] and not action.get_name().startswith('IA_Turn_')
    return sorted(expected_xr_keys)

validated_xr_keys = check_saved_bindings()
manifest_path = Path(r'E:/coding/ue proto/evidence/04/locomotion_input_manifest.json')
manifest_path.parent.mkdir(parents=True, exist_ok=True)
manifest_path.write_text(
    json.dumps({'priority':50, 'bindings':bindings, 'validated_xr_vector_keys':validated_xr_keys,
                'openxr_vector_binding_contract':'Axis2D -> thumbstick/trackpad parent, no XR swizzle',
                'template_action_names_before':template_action_names,
                'removed_template_locomotion':removed_template_bindings,
                'skipped_unregistered_template_menu_keys':skipped_menu_keys,
                'hardware_input_verified':False}, indent=2), encoding='utf-8')
unreal.log('GRATIA_LOCOMOTION_INPUT_CREATED')
