# Активная подготовка, импорт и сборка

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
6. `export_source_cloth_cages.py` / `audit_source_cloth_motion.py`: через живой
   Blender MCP экспорт исходных мягких клеток и наблюдение source simulation.
   После сборки editor-модуля `port_source_cloth_cages.py` переносит их в native
   Chaos Clothing Asset и профиль. Подробнее ниже.
7. `Build-Stage1.ps1`: versioned editor/runtime build и самостоятельный Windows-пакет.
   Не изменять Source/Config/Scripts/Content между stamp и окончанием сборки.
8. `Test-Stage1.ps1`, `Test-CharacterProfiles.ps1`, `Test-CharacterPoses.ps1`,
   `Test-CharacterViews.ps1`, `Test-CharacterReactions.ps1`: проверки именно готового
   пакета с привязкой к manifest. `Verify-Project.ps1` запускает основной набор.
   Аппаратные VR-проверки записываются отдельно.

Импорт/настройка Unreal выполняются через PythonScript commandlet установленного
редактора. Python, PowerShell и MCP не требуются готовой игре. Импорт не запускается,
пока игровые файлы открыты другим редактором или выполняется cook/package.

## Активный перенос исходной мягкой поверхности

Реализовано 5 октября 2026; редакторская и packaged QA пока ожидаются.
`export_source_cloth_cages.py` и
`audit_source_cloth_motion.py` выполняются только через живой Blender MCP в MVP-копии.
Первый сохраняет исходные клетки, Pin, bone weights и точные SurfaceDeform-маски;
второй временно включает их viewport-представление и восстанавливает состояние.
После сборки editor-модуля `port_source_cloth_cages.py` создаёт native Chaos Cloth
в существующем skeletal mesh из трёх включённых клеток TitsPhys/AssPhys/ThighsPhys
(3112 вершин, 206 full pins), сохраняет skeleton/material/morph ресурсы и отключает
дублирующую rigid-body симуляцию группы тела. Перед импортом сохраняется резервная
копия меша/профиля. Повторный запуск меняет только собственные cloth assets;
source pins, материалы и маски экспортируются в `Exports/Gratia/GameRig/source_cloth_cages.json`.
`-GratiaClothQA` проверяет частицы и видимые вершины при синтетическом нажиме,
хвате и сбросе; обязательна проверка конкретного нового пакета и отдельный VR-журнал.
Import marker подтверждает только выполнение скрипта.

Chaos требует адаптации source stiffness/pressure/internal springs. Активные
cloth-вершины подавляют обычные positional corrective morphs, исключённые лицо/руки
сохраняют skinning/morph path. Hair SurfaceDeform выключены в source render и
не переносятся. Native body cloth пока одинаков при Low/Medium/High;
костные группы аксессуаров продолжают использовать профильные quality caps.

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
