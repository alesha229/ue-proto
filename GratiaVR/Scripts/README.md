# Активная подготовка, импорт и сборка

Карта проекта — [README.md](../../README.md). Лобби, сцены и музыка (`setup_scene_experience.py`,
`prepare_user_music.py`, `generate_scene_music.py`, `import_scene_thumbnails.py`) —
[EXPERIENCE.md](../../docs/EXPERIENCE.md). Генератор сцен запускается только по `Build.cmd -RegenerateScenes`.

Ответ персонажа на касание (`DA_Gratia.ReactionLines`, см. [CHARACTER_PROFILE.md](../../docs/CHARACTER_PROFILE.md)):

1. Голос — один из двух источников в `Exports/Gratia/Audio/Reactions/` (WAV не в git, `reactions.json` — в git):
   - `prepare_reaction_voice_pack.py "<пак>/Processed"` — основной: нарезка записанного пака VoxAfterHours
     (демо WHSFX, автор — https://x.com/VoxAfterHours; лицензии в архиве нет, поэтому ни пак, ни нарезка, ни
     импортированные звуки в git не попадают) на одиночные звуки по паузам, 6 дублей на вид (вдох-испуг, мягкое
     «ах», «мм», вздох, смешок, «ara ara» на вопросы, стон), одинаковая громкость; `--plot sheet.png` — формы волн;
   - `generate_reaction_voice.py` — запасной: формантный синтез, если пака нет.
2. `setup_character_presentation.py` — коммандлет: импорт звуков в `/Game/Gratia/Audio/Reactions` (лишние удаляются),
   реплики (текст облачка, голос и остальные дубли вида как варианты, настроение, зоны или каналы, сила касания)
   и подписи поз свободной игры в `DA_Gratia`.

Каналы проникновения — `setup_penetration.py` (коммандлет): каналы `DA_Gratia` (кости стенок и внешнее кольцо таза и
ягодиц, глубина 26 и 36 см, профиль тугости) и `SK_Gratia_Game`:
- плотнее сетка у входов (`SubdivideMeshAroundBones`: треугольники в 9 см от костей входов делятся на 4, в 5 см — на 16,
  без трещин; каждый проход — один раз, по метке в описании меша);
- морфы раскрытия `Gratia_OpenVaginal` / `Gratia_OpenAnal` (`CreateChannelOpeningMorph`: до 6,2 см от оси, затухание
  12,5 см — меш не рвётся);
- вздутия живота `Gratia_Bulge*` (`CreateChannelBulgeMorph`: вперёд на 5–5,5 см при весе 1, до 1,25 у 3XL/4XL,
  ни вход, ни лобок, ни бёдра не трогает).

`-GratiaChannelShots` — снимки входов (пусто, три пальца, ладонь, кулак, две руки, XXL; формы ждут захвата у входа и
входят со скоростью 25 см/с) и живота до и после глубокого 4XL в `GratiaVR/Saved/Screenshots/ChannelShots`.

Ввод контроллеров — `setup_gratia_locomotion.py` (коммандлет): ходьба, поворот, хват, grip, меню,
`IA_Recenter` (клик любым стиком — центровка) и `IA_ThumbLeft/Right` (большой палец на стике или кнопке —
ладонь с прямыми пальцами для каналов; на клавиатуре B/N).

Звуки меню — `setup_menu_sounds.py` (коммандлет): копии штатных VR-звуков интерфейса движка
(клик, открытие, закрытие) в `/Game/Gratia/Audio/UI`, чтобы они попадали в сборку; отчёт
`evidence/04/menu_sounds.json`.

Игра без сборки пакета — `Play-Project.cmd [vr|desktop]` (ярлыки `Play-VR.cmd` и `Play-Desktop.cmd` в корне):
дособирает C++ модуль редактора (если редактор закрыт) и запускает игру из проекта с `-game`.
`-GratiaMenuShots` — снять все страницы меню в `GratiaVR/Saved/Screenshots/MenuShots` и выйти
(с `-GratiaScene=<Id>` — меню внутри сцены).

Мокап VaM (KM466, активный путь, см. [MOCAP_KM466.md](../../docs/MOCAP_KM466.md)):

1. `extract_vam_timeline.py` — чтение исходных VaM Timeline-кривых без изменений
   (`evidence/05/kitty_mocap/trial10s_samples.json`).
2. `author_vam_mocap.py` через `Blender-MCP.py code` с заголовком `PHASE = '...'`:
   `preview` → `lift` → `author` (`RANGE = (1, 60)` и т. д., частями) → `validate` →
   `correct` → `save` → `export`. Для `correct` нужен `GRATIA_MCP_TIMEOUT=1200`.
3. `import_vam_mocap.py` — editor-коммандлет: переимпорт `SK_Gratia_Game` с формами
   `Game_KM466_*`, импорт `A_Gratia_Game_KM466`, запись в `DA_Gratia.PerformanceClips`.

Полная запись (566,9 с): `LONG = True` в заголовке `author_vam_mocap.py`. Фазы, каждая
короче лимита MCP 300 с: `collect` (`RANGE` по 600 кадров) → `basis_mask` / `basis_rows` →
`fit_mocap_basis.py 64` (системный Python, вне Blender) → `basis_restore` → `save` → по частям
`segment_setup` / `segment_coef` (`RANGE` по 600) / `segment_finish` (`ALLOW_FAILED = True`:
общий базис не проходит допуск 2 мм) / `segment_export` → `save`. Затем
`GRATIA_MOCAP_LONG=1` `import_vam_mocap.py` (части `A_Gratia_Game_KM466Full_SNN`, перформанс
`KM466 Full`).

Сцена (партнёр, музыка, вид партнёра): `extract_vam_scene_partner.py <scene.json>
<samples.json> <out.json>` (системный Python), ffmpeg `music over anim.mp3` →
`Exports/Gratia/Audio/KM466_Music.wav` (44,1 кГц, стерео, не в git), затем коммандлет
`import_performance_scene.py` (после каждого импорта перформансов: он заново заполняет `Scene`).

Мягкие области игрового рига (как общие клетки Blender TitsPhys/AssPhys/ThighsPhys):
`author_soft_regions.py` через Blender MCP с заголовком `PHASE = '...'`: `restore` (из копии
до правок) → `apply` (веса мягких костей одежды по коже, верх над грудью — «вторая кожа»,
кожа под плотной тканью на ≥2,5 мм внутрь, кости `DEF-thigh_soft`) → `validate` → `save` →
`export` (`Gratia_Game_mesh.fbx`). Затем коммандлеты `reimport_game_mesh.py` (меш, скелет
дополняется, ожидаемые количества профиля) → `setup_soft_body.py` (без коллайдеров рук самой
героини) → `setup_body_surface.py` (ягодица — сфера по всей части, грудь и голова — по ядру)
→ `setup_soft_press_material.py` (вмятина: 4 слота, ближайшие к рукам зоны)
→ `setup_spring_bones.py` → `setup_penetration.py` (каналы примитива в `DA_Gratia.Penetration`:
кости входа и стенок `DEF-ero_vag_*`, `DEF-ero_ass_*`, внешнее кольцо `DEF-pelvis_*`/`DEF-ass_*`;
отчёт `evidence/06/penetration_setup.json`). Путь к скрипту в `-script=` передавать с прямыми
слешами: обратный слеш перед `ue` в `E:\coding\ue proto` портится при разборе командной строки.

Волосы, галстук, бантики сапог, бахрома эполет, уши и хвост — пружинные цепочки KawaiiPhysics
(`setup_spring_bones.py`: цепочки из `physics_group_manifest.json` и эполет, ось костей
измеряется, Chaos для этих костей выключается, отчёт `evidence/05/spring_chain_setup.json`).

`correct_kitty_mocap.py` и `preview_kitty_mocap.py` — **исторические** скрипты второй
отклонённой попытки (GPT, 5 октября); остальные её скрипты лежат в
`evidence/05/kitty_mocap/failed_scripts`. Не запускать.

Обновление 4 октября: wrist-roll reference authoring исправлен без изменения
порогов QA; source-shape actions используются по фактически возвращённым именам.
Новые клипы прошли integer/half-frame QA (максимум 0,679 мм), прежние пять — регрессию.
Импорт использует свежие экспорты и SHA-256. Setup locomotion пропускает
незарегистрированные menu keys шаблона и проверяет каждый создаваемый key.
Self-test включает синтетическое воздействие обеих рук на настоящие Chaos bodies;
это отдельно от аппаратной приёмки.

Все операции с моделью, ригом, формами, анимациями и FBX из Blender выполняются
через `Blender-MCP.py code --code-file ...` в открытом `Exports/Gratia/Gratia_mvp.blend`.
Прямой запуск этих bpy-скриптов отдельным Blender не является активным способом работы.
Исходные blend-файлы не изменяются. Правила: [AGENTS.md](../../AGENTS.md).

## Текущая последовательность

После импорта `repair_secondary_shape_units.py` однократно адаптирует размеры
существующего PhysicsAsset к FBX bone scale; резервная копия находится в evidence/04.
Новая генерация `BuildPhysicsAsset` также выполняет эту конверсию.
IA_GrabLeft/Right используют trigger Axis1D; Z/X — только desktop QA.
Перед выдачей manifest сборка сравнивает все staged/archive файлы по SHA-256.

1. `Start-BlenderMCP.ps1` / `Blender-MCP.py`: подключение к открытому Blender,
   проверка рабочего файла и журнал MCP-вызовов.
2. `author_reference_reactions.py`: создание трёх реакций в MVP-копии.
   `author_reference_correctives.py`: дополнительные формы для переноса DQS/B-Bones
   в линейный игровой риг; проверка новых и прежних клипов на целых и половинных кадрах.
   Экспорт разрешается только после успешной проверки; отчёт содержит SHA-256 FBX.
3. `import_reference_reactions.py`: импорт проверенного mesh и трёх реакций в Unreal.
   Существующие skeleton, порядок костей, materials и PhysicsAsset сохраняются.
   Проверяются исходные морфы, signed корректирующие кривые и мимика; обновляется
   только маршрутизация реакций и соответствующие поля `DA_Gratia`.
4. `create_character_profiles.py`: создание отсутствующих профилей либо проверка
   существующих. Параметр `-GratiaRegenerateProfiles` явно заменяет профильные настройки.
   `configure_character_stage.py`: явные ссылки карты на профиль, цель и куб контакта.
5. `setup_gratia_locomotion.py`: обновление Input Actions/context и регистрация
   OpenXR vector2-привязок; проверка всех зарегистрированных ключей перед сохранением.
6. `export_source_cloth_cages.py` (через живой Blender MCP) экспортирует коллайдеры
   Body/Head collision; `setup_soft_body.py` настраивает KawaiiPhysics в профиле. Подробнее ниже.
7. `Build-Stage1.ps1`: versioned editor/runtime build и самостоятельный Windows-пакет.
   Не изменять Source/Config/Scripts/Content между stamp и окончанием сборки.
8. `Test-Stage1.ps1`, `Test-CharacterProfiles.ps1`, `Test-CharacterPoses.ps1`,
   `Test-CharacterViews.ps1`, `Test-CharacterReactions.ps1`: проверки именно готового
   пакета с привязкой к manifest. `Verify-Project.ps1` запускает основной набор.
   Аппаратные VR-проверки записываются отдельно.

Импорт/настройка Unreal выполняются через PythonScript commandlet установленного
редактора. Python, PowerShell и MCP не требуются готовой игре. Импорт не запускается,
пока игровые файлы открыты другим редактором или выполняется cook/package.

## Мягкие части: KawaiiPhysics (активный путь)

С 5 октября 2026 мягкие грудь и ягодицы — цепочки KawaiiPhysics 1.21.0 (плагин в
`GratiaVR/Plugins/KawaiiPhysics`, MIT), а не Chaos Cloth. Схема как у VRChat PhysBones:
кости с пружиной и коллизией, сферы ладони/пальцев толкают их, триггер тянет кончик.
`setup_soft_body.py` (editor commandlet) заполняет `DA_Gratia.SoftBody`: по коже меша
измеряет ось/длину/зону контакта каждой кости, а из Blender Body/Head collision
(`export_source_cloth_cages.py`, schema 2, через Blender MCP) строит сферы коллайдеров
тела. Физические значения цепочек — данные профиля, правятся в Details.
`Test-CharacterSoftBody.ps1` запускает `-GratiaSoftBodyQA` в готовом пакете.
Скрипты Chaos Cloth (`port_source_cloth_cages.py`, `Test-CharacterCloth.ps1`) удалены;
`audit_source_cloth_motion.py` оставлен как исторический замер источника.

## Исторические миграции

Следующие скрипты сохраняют воспроизводимость предыдущих этапов, но их повторный
запуск поверх текущих ресурсов может заменить настройки художника:

- Первоначальный перенос: `export_gratia_fbx.py`, `export_gratia_animations.py`,
  `import_gratia_preview.py`, `update_gratia_animation.py`, `place_gratia_preview.py`.
- Создание первого чистого GameRig: `prepare_gratia_game_rig.py`,
  `bake_gratia_game_rig_clip.py`, `correct_gratia_game_rig.py`,
  `validate_corrected_game_rig_clip.py`, `import_gratia_game_rig.py`.
- Первые условные реакции: `export_gratia_reactions.py`, `import_gratia_reactions.py`.
  Они остаются регрессионными ресурсами; текущие контакты выбирают новые профильные клипы.
- Первоначальные сцена, физика и материалы: `create_stage1_room.py`,
  `place_gratia_game_rig.py`, `port_gratia_physics.py`, `fix_gratia_vr_appearance.py`,
  `port_gratia_toon_normals.py`, `fix_gratia_material_usage.py`.

`inspect_*`, `audit_*`, `check_gratia_morphs.py` и `generate_secondary_bones.py`
остаются вспомогательными диагностическими/editor-only инструментами. Их результат
не заменяет проверку финального пакета или реального контроллера.
