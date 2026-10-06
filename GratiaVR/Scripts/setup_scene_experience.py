"""Author the editable scene catalogue and two environments; run after the editor build.

Only development tooling uses Python. Runtime loads cooked Unreal assets. Existing
character meshes, profiles and the calibration test geometry are retained.
"""
import array
import hashlib
import json
import math
import wave
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'evidence/experience'
OUT.mkdir(parents=True, exist_ok=True)
BASE = '/Game/Gratia/Experience'
LIB = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LEVELS = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EDIT = unreal.MaterialEditingLibrary
AUTHORING_HASH = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
input_hash = hashlib.sha256()
for authoring_input in (ROOT / 'GratiaVR/Content/Characters/Profiles/DA_Gratia.uasset', ROOT / 'Exports/Gratia/Audio/KM466_Music.wav'):
    input_hash.update(str(authoring_input.relative_to(ROOT)).encode())
    if authoring_input.is_file():
        with authoring_input.open('rb') as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                input_hash.update(chunk)
INPUT_HASH = input_hash.hexdigest()
required_assets = ['DA_SceneLibrary', 'MPC_Music', 'S_Ambient', 'DA_AmbientAnalysis',
                   'Materials/M_LoadingSky', 'Materials/M_Glow', 'Materials/M_Floor',
                   'Materials/M_Architecture', 'Materials/M_ReactiveTrim',
                   'Environments/L_Atrium', 'Environments/L_PulseStudio']
if (ROOT / 'Exports/Gratia/Audio/KM466_Music.wav').is_file():
    required_assets.append('DA_KM466Analysis')
existing_library = unreal.load_asset(BASE + '/DA_SceneLibrary') if LIB.does_asset_exist(BASE + '/DA_SceneLibrary') else None
SHOULD_AUTHOR = not (existing_library and LIB.get_metadata_tag(existing_library, 'GratiaExperienceAuthoring') == AUTHORING_HASH
        and LIB.get_metadata_tag(existing_library, 'GratiaExperienceInputs') == INPUT_HASH
        and all(LIB.does_asset_exist(BASE + '/' + name) for name in required_assets))

if SHOULD_AUTHOR:
    def props(obj, **values):
        for key, value in values.items():
            obj.set_editor_property(key, value)
        return obj


    def data(name, cls):
        obj = unreal.load_asset(BASE + '/' + name) if LIB.does_asset_exist(BASE + '/' + name) else None
        if obj:
            assert isinstance(obj, cls), name
            return obj
        factory = props(unreal.DataAssetFactory(), data_asset_class=cls)
        return TOOLS.create_asset(name, BASE, cls, factory)


    def material(name, color, unlit=False, reactive=False, sky=False):
        path = BASE + '/Materials/' + name
        mat = unreal.load_asset(path) if LIB.does_asset_exist(path) else TOOLS.create_asset(name, BASE + '/Materials', unreal.Material, unreal.MaterialFactoryNew())
        EDIT.delete_all_material_expressions(mat)
        props(mat, two_sided=sky, shading_model=unreal.MaterialShadingModel.MSM_UNLIT if unlit else unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
        c = props(EDIT.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -600, 0), parameter_name='Accent' if sky else 'Color', default_value=unreal.LinearColor(*color, 1))
        if unlit:
            intensity = props(EDIT.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -600, 140), parameter_name='Intensity', default_value=.4 if sky else 1.0)
            mul = EDIT.create_material_expression(mat, unreal.MaterialExpressionMultiply, -100, 0)
            EDIT.connect_material_expressions(c, '', mul, 'A')
            EDIT.connect_material_expressions(intensity, '', mul, 'B')
            if sky:
                # A dark sky with a slow travelling gradient, centred on the loading space.
                normal = EDIT.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -900, -250)
                mask = props(EDIT.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -700, -250), r=False, g=False, b=True, a=False)
                EDIT.connect_material_expressions(normal, '', mask, 'Input')
                weight = props(EDIT.create_material_expression(mat, unreal.MaterialExpressionMultiply, -450, -250), const_b=.3)
                EDIT.connect_material_expressions(mask, '', weight, 'A')
                add = props(EDIT.create_material_expression(mat, unreal.MaterialExpressionAdd, -250, -250), const_b=.45)
                EDIT.connect_material_expressions(weight, '', add, 'A')
                gradient = EDIT.create_material_expression(mat, unreal.MaterialExpressionMultiply, 50, 0)
                EDIT.connect_material_expressions(mul, '', gradient, 'A')
                EDIT.connect_material_expressions(add, '', gradient, 'B')
                mul = gradient
            if reactive:
                energy = props(EDIT.create_material_expression(mat, unreal.MaterialExpressionCollectionParameter, -600, 240), collection=collection, parameter_name='Energy')
                gain = props(EDIT.create_material_expression(mat, unreal.MaterialExpressionAdd, -350, 240), const_b=.25)
                EDIT.connect_material_expressions(energy, '', gain, 'A')
                pulse = EDIT.create_material_expression(mat, unreal.MaterialExpressionMultiply, 160, 0)
                EDIT.connect_material_expressions(mul, '', pulse, 'A')
                EDIT.connect_material_expressions(gain, '', pulse, 'B')
                mul = pulse
            EDIT.connect_material_property(mul, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        else:
            EDIT.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
            rough = props(EDIT.create_material_expression(mat, unreal.MaterialExpressionConstant, -500, 160), r=.7)
            EDIT.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
        EDIT.recompile_material(mat)
        assert LIB.save_loaded_asset(mat, only_if_is_dirty=False)
        return mat


    collection = unreal.load_asset(BASE + '/MPC_Music') if LIB.does_asset_exist(BASE + '/MPC_Music') else TOOLS.create_asset('MPC_Music', BASE, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    collection.set_editor_property('scalar_parameters', [props(unreal.CollectionScalarParameter(), parameter_name=unreal.Name(name), default_value=0.) for name in ('Bass', 'Mid', 'High', 'Beat', 'Energy')])
    assert LIB.save_loaded_asset(collection, only_if_is_dirty=False)
    sky_mat = material('M_LoadingSky', (.025, .014, .055), unlit=True, sky=True)
    glow_mat = material('M_Glow', (.22, .12, .4), unlit=True)
    floor_mat = material('M_Floor', (.075, .09, .12))
    wall_mat = material('M_Architecture', (.18, .20, .25))
    trim_mat = material('M_ReactiveTrim', (.15, .40, .58), unlit=True, reactive=True)
    sphere = unreal.load_asset('/Engine/BasicShapes/Sphere')
    cylinder = unreal.load_asset('/Engine/BasicShapes/Cylinder')
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')
    assert sphere and cylinder and cube

    # A quiet original 24-second loop, generated deterministically. No third-party audio.
    wav = OUT / 'GratiaAmbient.wav'
    sample_rate = 22050
    duration = 24.
    samples = array.array('h')
    for index in range(int(sample_rate * duration)):
        t = index / sample_rate
        beat = t % .75
        chord = int(t / 6) % 4
        fundamental = (130.8128, 164.8138, 146.8324, 110.)[chord]
        pad = sum(math.sin(2 * math.pi * fundamental * ratio * t) for ratio in (1, 1.5, 2)) / 3
        edge = min(1., (t % 6) / .6, (6 - t % 6) / .6)
        kick = math.sin(2 * math.pi * 58 * t) * math.exp(-beat * 22)
        value = .10 * pad * edge + .13 * kick
        samples.append(int(max(-1, min(1, value)) * 32767))
    with wave.open(str(wav), 'wb') as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(sample_rate)
        output.writeframes(samples.tobytes())
    task = props(unreal.AssetImportTask(), filename=str(wav), destination_path=BASE, destination_name='S_Ambient', automated=True, replace_existing=True, save=True)
    TOOLS.import_asset_tasks([task])
    sound = unreal.load_asset(BASE + '/S_Ambient')
    assert isinstance(sound, unreal.SoundWave)
    props(sound, looping=True)
    assert LIB.save_loaded_asset(sound, only_if_is_dirty=False)
    analysis = data('DA_AmbientAnalysis', unreal.GratiaMusicAnalysis)
    vector_type = getattr(unreal, 'Vector4f', unreal.Vector4)
    def vector4(x, y, z, w):
        return props(vector_type(), x=float(x), y=float(y), z=float(z), w=float(w))
    frames = [vector4(.15 + .7 * math.exp(-(frame / 30 % .75) * 22), .22 + .05 * math.sin(frame / 80), .1, math.exp(-(frame / 30 % .75) * 12)) for frame in range(int(duration * 30))]
    props(analysis, sound=sound, frames_per_second=30., beats_per_minute=80., frames=frames)
    assert LIB.save_loaded_asset(analysis, only_if_is_dirty=False)


    def mesh(name, shape, position, scale, mat, collision=False, rotation=None):
        actor = ACTORS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*position), rotation or unreal.Rotator())
        actor.set_actor_label(name)
        actor.static_mesh_component.set_static_mesh(shape)
        actor.static_mesh_component.set_material(0, mat)
        actor.static_mesh_component.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
        actor.set_actor_scale3d(unreal.Vector(*scale))
        return actor


    def environment(name, accent, courtyard):
        path = BASE + '/Environments/' + name
        if LIB.does_asset_exist(path):
            assert LEVELS.load_level(path)
            for actor in ACTORS.get_all_level_actors():
                if actor.get_actor_label().startswith('Experience_'):
                    assert ACTORS.destroy_actor(actor)
        else:
            assert LEVELS.new_level(path)
        mesh('Experience_Floor', cube, (0, 0, -10), (12, 12, .2), floor_mat, True)
        mesh('Experience_BackWall', cube, (550, 0, 180), (.2, 12, 3.6), wall_mat, True)
        for sign in (-1, 1):
            mesh('Experience_SideWall' + str(sign), cube, (0, sign * 600, 180), (12, .2, 3.6), wall_mat, True)
            for x in (-450, -150, 150, 450):
                mesh('Experience_Pillar' + str((x, sign)), cylinder, (x, sign * 520, 180), (.38, .38, 3.6), wall_mat, True)
                mesh('Experience_Accent' + str((x, sign)), cube, (x, sign * 496, 190), (.10, .06, 3.0), trim_mat)
        if not courtyard:
            mesh('Experience_Ceiling', cube, (0, 0, 400), (12, 12, .2), wall_mat)
        else:
            for x in range(-400, 500, 100):
                mesh('Experience_Rafter' + str(x), cube, (x, 0, 410), (.10, 12, .10), trim_mat)
        for y in (-240, 240):
            mesh('Experience_FloorTrace' + str(y), cube, (0, y, .25), (9, .025, .005), trim_mat)
        sun = ACTORS.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 300), unreal.Rotator(pitch=-45, yaw=-20, roll=0))
        sun.set_actor_label('Experience_KeyLight')
        props(sun.light_component, intensity=2.5, cast_shadows=False)
        sun.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        skylight = ACTORS.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 300))
        skylight.set_actor_label('Experience_FillLight')
        props(skylight.light_component, intensity=.8, lower_hemisphere_is_black=False)
        skylight.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        for index, pos in enumerate(((150, -230, 200), (150, 230, 200))):
            light = ACTORS.spawn_actor_from_class(unreal.PointLight, unreal.Vector(*pos))
            light.set_actor_label('Experience_MusicLight' + str(index))
            props(light, tags=[unreal.Name('GratiaAudioLight'), unreal.Name('GratiaAudioMid' if index else 'GratiaAudioHigh')])
            props(light.light_component, intensity=800., cast_shadows=False, attenuation_radius=330.)
            light.light_component.set_light_color(unreal.LinearColor(*accent, 1))
            light.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        for tag, pos, yaw in (('GratiaCharacterSpot', (140, 0, 0), 90), ('GratiaPlayerSpot', (-30, 0, 0), 0)):
            marker = ACTORS.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(*pos), unreal.Rotator(pitch=0, yaw=yaw, roll=0))
            marker.set_actor_label('Experience_' + tag)
            props(marker, tags=[unreal.Name(tag)])
        assert LEVELS.save_current_level()
        return path


    atrium = environment('L_Atrium', (.35, .65, 1.), True)
    pulse = environment('L_PulseStudio', (1., .3, .55), False)
    profile = unreal.load_asset('/Game/Characters/Profiles/DA_Gratia')
    assert profile
    performances = profile.get_editor_property('performance_clips')
    entries = []


    def entry(id, title, description, env, accent, performance=''):
        obj = props(unreal.GratiaSceneEntry(), id=unreal.Name(id), title=title, description=description,
                    environment=unreal.load_asset(env), accent=unreal.LinearColor(*accent, 1), performance=unreal.Name(performance),
                    music=analysis, music_volume=.55)
        preview = BASE + '/Previews/T_' + ('PulseStudio' if env == pulse else 'Atrium')
        if LIB.does_asset_exist(preview):
            obj.set_editor_property('thumbnail', unreal.load_asset(preview))
        entries.append(obj)


    entry('Atrium', 'Атриум', 'Свободное взаимодействие · мягкий свет', atrium, (.3, .65, .9))
    entry('Pulse', 'Ритм', 'Музыка и свет · свободное взаимодействие', pulse, (.9, .3, .55))
    performance_analysis = None
    source_music = ROOT / 'Exports/Gratia/Audio/KM466_Music.wav'
    if source_music.is_file():
        # Three complementary one-pole bands, measured from the existing imported track.
        # Offline envelope analysis, not runtime FFT. Beat = bass onset with 200 ms refractory.
        with wave.open(str(source_music), 'rb') as source:
            assert source.getsampwidth() == 2, 'Expected the existing 16-bit PCM development input'
            rate, channels = source.getframerate(), source.getnchannels()
            pcm = array.array('h', source.readframes(source.getnframes()))
        stride = max(1, rate // 6000)
        effective_rate = rate / stride
        alow = 1 - math.exp(-2 * math.pi * 120 / effective_rate)
        amid = 1 - math.exp(-2 * math.pi * 1000 / effective_rate)
        low = middle = 0.
        sums = [0., 0., 0.]
        count = 0
        envelopes = []
        next_frame = 1
        for sample in range(0, len(pcm) // channels, stride):
            value = sum(pcm[sample * channels + channel] for channel in range(channels)) / (channels * 32768)
            low += alow * (value - low)
            middle += amid * (value - middle)
            for band, component in enumerate((low, middle - low, value - middle)):
                sums[band] += component * component
            count += 1
            if sample / rate >= next_frame / 30:
                envelopes.append([math.sqrt(total / max(1, count)) for total in sums])
                sums, count = [0., 0., 0.], 0
                next_frame += 1
        scales = [max(.001, sorted(row[band] for row in envelopes)[int(len(envelopes) * .95)]) for band in range(3)]
        pulse_value, previous, last_onset = 0., 0., -10
        measured = []
        for frame, row in enumerate(envelopes):
            bands = [max(0., min(1., row[band] / scales[band])) for band in range(3)]
            pulse_value *= math.exp(-1 / 30 * 12)
            if bands[0] > .4 and bands[0] - previous > .15 and frame - last_onset >= 6:
                pulse_value, last_onset = 1., frame
            previous = bands[0]
            measured.append(vector4(*bands, pulse_value))
        performance_analysis = data('DA_KM466Analysis', unreal.GratiaMusicAnalysis)
        props(performance_analysis, sound=unreal.load_asset('/Game/Gratia/Performance/S_KM466_Music'), frames_per_second=30., frames=measured)
        assert LIB.save_loaded_asset(performance_analysis, only_if_is_dirty=False)
    for index, clip in enumerate(performances):
        name = str(clip.get_editor_property('name'))
        if clip.get_editor_property('clip') or clip.get_editor_property('segments'):
            entry('Performance_' + str(index), name, 'Запись анимации · управление воспроизведением', pulse if index % 2 else atrium, (.65, .35, .9), name)
            if performance_analysis and clip.get_editor_property('scene').get_editor_property('music') == performance_analysis.get_editor_property('sound'):
                entries[-1].set_editor_property('performance_music', performance_analysis)
            entries[-1].set_editor_property('haptic_bone', unreal.Name('Pelvis'))
    library = data('DA_SceneLibrary', unreal.GratiaSceneLibrary)
    props(library, scenes=entries, lobby_music=analysis, lobby_music_volume=.4,
          audio_collection=collection, loading_sky_material=sky_mat,
          glow_material=glow_mat, sphere_mesh=sphere, cylinder_mesh=cylinder,
          min_loading_seconds=1.6, fade_seconds=.45)
    assert LIB.save_loaded_asset(library, only_if_is_dirty=False)
    assert LEVELS.load_level('/Game/Gratia/Maps/L_Stage1')
    runtime = [actor for actor in ACTORS.get_all_level_actors() if isinstance(actor, unreal.GratiaStage1Runtime)]
    assert len(runtime) == 1, 'An explicit unique runtime actor is required'
    runtime[0].get_editor_property('scene_director').set_editor_property('library', library)
    for actor in ACTORS.get_all_level_actors():
        label = actor.get_actor_label()
        if label in ('Floor_6m', 'BackWall', 'LeftWall', 'RightWall', 'ScaleCube_Exactly1m', 'HeightMarker_165cm', 'Stage1Instructions', 'ScaleCubeLabel', 'HeightMarkerLabel', 'Stage1Sun', 'Stage1Sky'):
            tags = list(actor.get_editor_property('tags'))
            if unreal.Name('GratiaStudio') not in tags:
                tags.append(unreal.Name('GratiaStudio'))
            actor.set_editor_property('tags', tags)
    assert LEVELS.save_current_level()
    LIB.set_metadata_tag(library, 'GratiaExperienceAuthoring', AUTHORING_HASH)
    LIB.set_metadata_tag(library, 'GratiaExperienceInputs', INPUT_HASH)
    assert LIB.save_loaded_asset(library, only_if_is_dirty=False)
    report = dict(library=library.get_path_name(), scenes=[str(item.get_editor_property('id')) for item in entries],
                  environments=[str(atrium), str(pulse)], original_audio_sha256=hashlib.sha256(wav.read_bytes()).hexdigest(),
                  audio_note='Original deterministic pad/kick loop; amplitude-derived frames, no claim of live FFT.', runtime_python=False)
    (OUT / 'scene_assets.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('GRATIA_EXPERIENCE_ASSETS_READY ' + json.dumps(report, ensure_ascii=False))

else:
    unreal.log("GRATIA_EXPERIENCE_ASSETS_READY unchanged authoring=" + AUTHORING_HASH)
