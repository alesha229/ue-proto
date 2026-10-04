# Blender MCP — 4 октября 2026

Используется уже установленный официальный экспериментальный Blender Lab MCP,
а не второй сторонний сервер: `https://projects.blender.org/lab/blender_mcp.git`,
коммит `2cea8d566dde07fbac28a61d698909d69724e853`. Версия пакета/аддона — 1.0.3;
аддон требует Blender 5.1+, подключён Blender 5.2.1 LTS. Поле `serverInfo.version`
в MCP initialize возвращает версию библиотеки FastMCP 1.30.0; это не версия аддона.

Мост Blender слушает `127.0.0.1:9876`; HTTP MCP-сервер —
`http://127.0.0.1:8100/`. Codex уже содержит enabled-сервер `blender` с этим URL.
Unreal MCP отдельно использует `http://127.0.0.1:8000/mcp`.

Живые инструменты работают и без перезапуска текущего чата: сохранены MCP
initialize (HTTP 200), 26 инструментов, чтение сцены и сохранение отдельной копии
через `execute_blender_code`. Инструмент исполняет bpy внутри подключённого
Blender; Python здесь является содержимым вызова MCP. Если новые инструменты
не появились в текущем списке Codex, можно перезапустить соединение `blender`
в настройках позже; это не блокирует работу через проверенный MCP-транспорт.

Текущая сессия работает с `Exports/Gratia/Gratia_mvp.blend`. Она создана через
MCP Save As; исходные `Gratia.blend`, `Gratia_source.blend`, `Gratia_working.blend`
не сохранялись. Сверка SHA-256 до/после находится в `evidence/03/blender_mcp/`.
Перезапускать канал можно `Start-BlenderMCP.cmd`: он использует эту отдельную
копию, переиспользует уже слушающие порты и запускает новые помощники скрыто.

Клиент доказательств:

```powershell
python evidence/03/blender_mcp/verify_blender_mcp.py list --name tools
python evidence/03/blender_mcp/verify_blender_mcp.py call --tool get_blendfile_summary_path_info --name path
python evidence/03/blender_mcp/verify_blender_mcp.py code --code-file checked_script.py --name operation
```

Запросы и ответы пишутся в JSON без преобразования в обычные CLI-вызовы Blender.
Полный аудит 920 костей, 244 deform-костей, весов и материала сохранён в
`evidence/03/blender_mcp/full_rig_material_audit.json`; параметры шейдеров —
`material_input_audit.json`. Исходная одежда использует Emission Strength=1,
Principled Weight=0 и toon-result: чисто физическое освещение UE не повторяет
её внешний вид автоматически.

Новый кандидат отдельного игрового скелета находится в `Exports/Gratia/GameRig/`.
Его нельзя считать принятым только по количеству костей или наличию FBX;
`game_rig_manifest.json` содержит сравнение деформации и `runtime_approved`.

## Проверенный игровой экспорт

Вместо промежуточных 452 костей создан отдельный скелет root +244 DEF (245 bones
в FBX; импортёр может добавить armature root). Убранные Rigify controls остаются
на исходном риге рабочей копии, а игровому скелету запечены evaluated matrices
с корректным пересчётом новой родительской иерархии. Head/tail задаются до
rest matrix, иначе Blender создаёт ошибочный 180° roll; провал и исправление
сохранены в evidence.

Линейный скелет сам по себе совпал с прежним FBX, но исходные B-Bone/DQS давали
до 7,6 мм отклонения. Добавлены девять компактных корректирующих форм — общий
Neutral, четыре Arms, четыре Head. Они решены через обратную weighted LBS
матрицу в rest space; оригинальные 50 рабочих морфов сохранены. Нулевая Horny
сохраняется в FBX как канал без геометрической дельты, Unreal вправе его исключить.

Проверка всех 72 376 вершин каждого целого и половинного кадра относительно
полного исходного Blender DQS/B-Bone: Idle — максимум0,7604мм /361samples,
Arms —0,1185мм /241, Head —0,0365мм /241. Root и стопы не дрейфуют. Это приёмка
трёх авторских клипов; новые позы и комбинации мимики требуют своих проверок.
Рабочие `Gratia_preview.fbx`/старые анимации не заменены. Новый экспорт находится
в `Exports/Gratia/GameRig/Gratia_Game_*.fbx`; нужен новый Unreal Skeleton.

## Исходная физика

Через MCP записан `physics_group_manifest.json`:34группы /150 deform bones,
семь cloth/collision helpers, настоящие массы, stiffness/damping, pin groups,
cache frames, constraints/drivers и rest axes. Все150 костей есть в кандидате.
Pants/Pants decor не имеют отдельного skirt skeleton; их физические helpers
связаны с нижним телом. Четыре DEF-thigh bones — основные ноги, поэтому сохраняют
кинематическую опору; Blender ThighsPhys не равен rigid-body раскачиванию всей ноги.

`GratiaPortLibrary::BuildPhysicsAsset` — editor authoring helper для сохранённого
Chaos PhysicsAsset; runtime не вызывает создание ассетов. `GratiaSecondaryBones.h`
генерируется из manifest, содержит все150 нормализованных имён и146 безопасных
ролей вторичного движения. Joint bounds/драйвы и массы Chaos настроены отдельно;
это перенос назначения групп, а не прямой экспорт cloth/driver алгоритмов Blender.
Руки взаимодействуют через собственные query proxies проекта.
