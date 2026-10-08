"""How DA_Gratia presents itself to the player (commandlet): reaction lines and stance labels.

Reaction lines: speech-bubble text and voice per mood, zone and touch force.

Imports the synthesized voice (generate_reaction_voice.py -> Exports/Gratia/Audio/Reactions/*.wav) as
/Game/Gratia/Audio/Reactions/S_<Kind>_<n> and writes DA_Gratia.ReactionLines (FGratiaReactionLine). The presenter
picks the most specific match: a fast touch, then the touched zone or penetration channel, then the mood.
Moods: 0 calm, 1 cheerful, 2 reserved. Every line has one of the kind's voice variants (rotating).

Stances: free-play Pose switches between idle and looping clips without partner or music; their Label is
what the menu shows.

    UnrealEditor-Cmd GratiaVR.uproject -run=pythonscript -script=GratiaVR/Scripts/setup_character_presentation.py
"""
import json
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Exports/Gratia/Audio/Reactions'
FOLDER = '/Game/Gratia/Audio/Reactions'
PROFILE = '/Game/Characters/Profiles/DA_Gratia'
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
LIB = unreal.EditorAssetLibrary

CALM, CHEERFUL, RESERVED, ANY_MOOD = 0, 1, 2, -1
FACE = ['Face']
HAIR = ['Hair']
HANDS = ['Left hand', 'Right hand', 'Left forearm', 'Right forearm', 'Left shoulder', 'Right shoulder']
CHEST = ['Upper costume', 'Clothed torso']
HIPS = ['Waist fabric', 'Left fabric', 'Right fabric']
CHANNELS = ['Vaginal', 'Anal']

# (text, voice kind, mood, zones, force)
LINES = [
    ('Ммм…', 'hum', CALM, [], 'any'),
    ('Ах…', 'soft', CALM, [], 'any'),
    ('Хм?', 'ask', CALM, [], 'any'),
    ('Приятно…', 'hum', CALM, [], 'any'),
    ('Хи-хи!', 'giggle', CHEERFUL, [], 'any'),
    ('Щекотно!', 'giggle', CHEERFUL, [], 'any'),
    ('Ещё!', 'soft', CHEERFUL, [], 'any'),
    ('Хе-хе~', 'giggle', CHEERFUL, [], 'any'),
    ('Э-эй…', 'sigh', RESERVED, [], 'any'),
    ('Н-не надо…', 'sigh', RESERVED, [], 'any'),
    ('Стыдно…', 'sigh', RESERVED, [], 'any'),
    ('М-м…', 'hum', RESERVED, [], 'any'),
    # A fast touch startles her in any mood and anywhere.
    ('Ай!', 'startle', ANY_MOOD, [], 'strong'),
    ('Ой!', 'startle', ANY_MOOD, [], 'strong'),
    ('Осторожнее!', 'startle', ANY_MOOD, [], 'strong'),
    ('Что такое?', 'ask', CALM, FACE, 'gentle'),
    ('Хи-хи, щёчки!', 'giggle', CHEERFUL, FACE, 'gentle'),
    ('Н-не смотри так…', 'sigh', RESERVED, FACE, 'gentle'),
    ('Мои волосы…', 'soft', ANY_MOOD, HAIR, 'gentle'),
    ('Нравятся?', 'ask', CHEERFUL, HAIR, 'gentle'),
    ('Тёплая рука…', 'hum', CALM, HANDS, 'gentle'),
    ('Держи меня!', 'giggle', CHEERFUL, HANDS, 'gentle'),
    ('Р-рука…', 'sigh', RESERVED, HANDS, 'gentle'),
    ('Ах…', 'soft', CALM, CHEST, 'gentle'),
    ('Ой! Хи-хи', 'giggle', CHEERFUL, CHEST, 'gentle'),
    ('Т-туда нельзя…', 'sigh', RESERVED, CHEST, 'gentle'),
    ('Мм…', 'hum', CALM, HIPS, 'gentle'),
    ('Эй, юбка!', 'giggle', CHEERFUL, HIPS, 'gentle'),
    ('Не задирай…', 'sigh', RESERVED, HIPS, 'gentle'),
    ('Ах!', 'moan', ANY_MOOD, CHANNELS, 'any'),
    ('Ммх…', 'moan', ANY_MOOD, CHANNELS, 'any'),
    ('А-ах…', 'moan', ANY_MOOD, CHANNELS, 'any'),
    ('Глубже…', 'moan', ANY_MOOD, CHANNELS, 'any'),
]
FORCE = {'any': unreal.GratiaReactionForce.ANY, 'gentle': unreal.GratiaReactionForce.GENTLE, 'strong': unreal.GratiaReactionForce.STRONG}

manifest = json.loads((SOURCE / 'reactions.json').read_text(encoding='utf-8'))
voices = {}
for entry in manifest:
    name = 'S_' + entry['name']
    task = unreal.AssetImportTask()
    for key, value in dict(filename=str(SOURCE / (entry['name'] + '.wav')), destination_path=FOLDER, destination_name=name,
                           automated=True, replace_existing=True, save=True).items():
        task.set_editor_property(key, value)
    TOOLS.import_asset_tasks([task])
    sound = unreal.load_asset(f'{FOLDER}/{name}')
    assert isinstance(sound, unreal.SoundWave), name
    voices.setdefault(entry['kind'], []).append(sound)

profile = unreal.load_asset(PROFILE)
zones = {str(z.get_editor_property('name')) for z in profile.get_editor_property('contact_zones')}
channels = {str(c.get_editor_property('name')) for c in profile.get_editor_property('penetration').get_editor_property('channels')}
lines, turn = [], {}
for text, kind, mood, targets, force in LINES:
    unknown = [t for t in targets if t not in zones | channels]
    assert not unknown, f'Unknown zones or channels for "{text}": {unknown}'
    sound = voices[kind][turn.get(kind, 0) % len(voices[kind])]
    turn[kind] = turn.get(kind, 0) + 1
    line = unreal.GratiaReactionLine()
    line.set_editor_property('text', unreal.Text(text))
    line.set_editor_property('sound', sound)
    line.set_editor_property('mood', mood)
    line.set_editor_property('zones', [unreal.Name(t) for t in targets])
    line.set_editor_property('force', FORCE[force])
    lines.append(line)
profile.set_editor_property('reaction_lines', lines)
STANCE_LABELS = {'Idle ZZZ': 'игривая'}
clips = list(profile.get_editor_property('performance_clips'))
for clip in clips:
    label = STANCE_LABELS.get(str(clip.get_editor_property('name')))
    if label:
        clip.set_editor_property('label', unreal.Text(label))
profile.set_editor_property('performance_clips', clips)
assert LIB.save_loaded_asset(profile, only_if_is_dirty=False)
report = dict(stances=STANCE_LABELS, lines=len(lines), voices={k: len(v) for k, v in voices.items()}, zones=sorted(zones), channels=sorted(channels))
(ROOT / 'evidence/04/character_presentation.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('GRATIA_CHARACTER_PRESENTATION_READY ' + json.dumps(report, ensure_ascii=False))
