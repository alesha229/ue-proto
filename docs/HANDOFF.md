# Передача GratiaVR новому чату

## Актуальное продолжение — 5 октября 2026, cloth

Последний пакет `gratia-20261005T071828Z-26ceff42-facda857` (код `9964994`): Blender-клетки как native Chaos
Cloth, нажим/хват руками, привязка масок переживает cook. Desktop QA PASS (STATUS.md).
Следующий шаг — пользователь в шлеме: нажать ладонью грудь/ягодицы/бёдра, зажать
триггер и потянуть, отпустить; сохранить журнал. Настройки — DA_Gratia.ClothSettings
(SoftPressDepthCm, Grab*) и cloth config в ассете меша. После изменения клеток
в Blender повторить export_source_cloth_cages.py (MCP) → port_source_cloth_cages.py.

## Предыдущее продолжение — 5 октября 2026

Сначала читать свежий верхний блок STATUS.md. Исправлен wrist-roll разрыв исходного
Rigify предплечья, новые три реакции прошли MCP QA и импортированы (67 морфов).
Старый PASS FBX не использовался. Добавлены физический sweep/pressure и хват
триггерами обеих рук, отдельный HandInput; см. HAND_PHYSICS.md.
Главная найденная ошибка: размеры вторичных коллайдеров умножались на FBX bone
scale=100. PhysicsAsset исправлен, повторная генерация также использует bone-local units.
Готовый пакет `gratia-20261005T042144Z-4431921f-8a9e8aad`, код `07e3f78`,
прошёл архивную SHA-256 проверку. Результаты регрессии и длительного прогона — STATUS.md.
В manifest `git_dirty=true` из-за документационных правок и посторонних неотслеживаемых папок
`GratiaVR 5.8/` и `GratiaVR 5.8 - 2/`; их не изменяли. Активный проект — `GratiaVR/`.
Документационные коммиты после сборки не меняют runtime fingerprint.

Дальше нужна аппаратная проверка конкретного пакета: левый стик, обе руки,
триггеры около волос/ткани/тела, отпускание, tracking recovery и меню. Medium по
умолчанию включает только 32 кости волос; High включает все разрешённые 115.
Low отключает физику волос/ткани. Не объявлять desktop QA доказательством VR.
Выбраны штатные Chaos/PhysicalAnimation; дополнительный плагин не требуется
для реализованного ограниченного хвата. Защищённые Blender-оригиналы не менялись.

Далее — историческая передача checkpoint f5377fb; её незавершённые пункты
о неимпортированных реакциях, выборе плагина и отсутствии manifest устарели.

Дата: 4 октября 2026. Папка `E:\coding\ue proto`. **MVP не завершён.**
Прочитать AGENTS.md, STATUS.md, ARCHITECTURE.md, CHARACTER_PROFILE.md,
INPUT_DIAGNOSTICS.md и GratiaVR/Scripts/README.md. Не начинать проект заново.

## Задачи пользователя

Последний новый пункт: настроить физические кости готовыми средствами/плагинами
Unreal и дать взаимодействие VR-руками. Подбор/интеграция нового плагина ещё не
выполнены. Имеется ограниченная Chaos/PhysicalAnimation реализация, не подтверждённая
аппаратным тестом. Проверить доступные плагины и совместимость UE5.8.3; не считать
покупку платного плагина автоматически разрешённой. Основные ноги/позу держать
управляемыми, вторичную физику ограничивать группами; не включать полный ragdoll.

Также остаются прежние задачи: левый стик не работал в последнем VR-тесте,
при контакте персонаж лишь разводил руки, нужны реакции по видеореференсам,
правильные материалы/морфы и поддерживаемая замена персонажа. Работа авторизована;
не требовать повторного разрешения на эти исправления.

## Среда и сохранение

- UE `E:\ue\UE_5.8` версия5.8.3, проект GratiaVR/GratiaVR.uproject,
  карта `/Game/Gratia/Maps/L_Stage1`. Actor labels Gratia_Preview,
  Stage1CalibrationAndTrackingGuard; explicit target/куб уже сериализованы.
- Только `Exports/Gratia/Gratia_mvp.blend` редактируемый. Защищённые оригиналы
  Gratia.blend/Gratia_source.blend/Gratia_working.blend и backup не менять.
- Blender5.2 открыт. Настоящий Blender Lab MCP HTTP `http://127.0.0.1:8100/`.
  Клиент GratiaVR/Scripts/Blender-MCP.py; launcher Start-BlenderMCP.ps1.
  bpy только через MCP execute_blender_code; отдельный Blender --python запрещён.
- Живой незавершённый черновик сохранён через MCP как отдельная копия
  evidence/04/blender_mcp/Gratia_reference_unfinished_handoff.blend;
  handoff_state.json содержит filepath/actions. Не runtime-approved.
- UE Python E:/ue/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe;
  ffmpeg C:/ffmpeg/bin/ffmpeg.exe. RAM ограничена, compile jobs обычно1.
- Git/LFS baseline810da4c; передача сохраняется локальным checkpoint-коммитом,
  точный SHA получить git log -1. Remote/push не создавались. Evidence/Builds/Saved
  игнорируются, существуют локально; не удалять. Перед финальным stamp исходники
  и Scripts/Content должны быть стабильны; учитывать CRLF normalization Git.

## Выполненная архитектура

CharacterProfile DataAsset с mesh/clips/physics/semantic maps/capabilities/zones,
отдельными collision proxies, таймерами/бюджетами и таблицами реакций/звуков.
Подлинный второй скелет Epic Manny, explicit targets, независимый contact solver,
tracking/recovery gates, отдельные RuntimeVerification/EditorTools/ReactionPresentation.
Caption/audio теперь слушают OnContactReaction/OnContactReset, контакт не создаёт звук.
Runtime без Python/MCP/PS. Native SingleNode proxy остаётся; полный ручной AnimGraph
не реализован. Gratia PhysicsAsset176тел/146вторичных constrained, основные кости анимированы.

## Левый стик

setup_gratia_locomotion.py уже выполнен. Ранее Axis2D IA_Walk связывался со scalar
Thumbstick_X/Y; OpenXR требует parent /thumbstick vector2. Теперь4registered `_2D`
ключа Oculus/Index/WMR/Vive без XRswizzle. Legacy IA_Move/IA_Turn убраны из IMC_Default,
ValveIndex_Left_X_Click исправлен на Left_A_Click. Evidence04/input_bindings_fix.log
и locomotion_input_manifest.json. После новой сборки проверить реальный mapped input
и PawnDelta, отсутствие XR_ERROR_PATH_UNSUPPORTED. W-тест не подтверждает VR.
Legacy raw-key state в OpenXR отсутствует из-за InjectInputForAction, диагностика это отмечает.

Лог user_v05_vr_retest.log содержит реальные hand0/1 события и demo hand=-1 вместе,
а также path error. При скорости500 старый ReactBright разводил руки. Runtime теперь
выбирает Profile.ReactionClips→Default→Soft по зоне, скорость не выбирает openarms;
ReactionMinimumInterval0.45s. Новая DA_Gratia таблица ещё не заполнена до импорта.

## Референсные анимации: незавершённая проблема

Видео E:/downloads/Новая папка: Ashen Rabbit, EDDYSON_27 walking, Mister skyler cute.
Просмотрены evidence03/references/video_*_sheet.jpg, evidence04/reference_*_dense.jpg.
Новые жесты — адаптация по видео, не извлечённые ресурсы оригинальной игры. Ходьба
персонажа по видео ещё не добавлена.

Tracked scripts author_reference_reactions.py и author_reference_correctives.py
создают ReactFace2s/ReactHand2.4s/ReactCheer2.4s. Wrist/finger улучшены, Rigify curl
через master.scale.y=(1−angle/pi); перед neutral явный reset пальцев/запястий.

Первая версия PASS Face0.071мм/Hand0.142мм/Cheer0.324мм +10новыхcorrectives
(4Hand+6Cheer) к50source+9old=69непустых. Но wrists были вниз; версия не финальная.
После улучшения orientation/curl последний PCA FAIL до45.7мм Body/Gloves.
Прямые8-mode коэффициенты дают~0.043мм, анимированные каналы не воспроизводят результат.
Нельзя повышать допуск/ранг вслепую или пропустить QA.

Последняя диагностика в evidence/04/blender_mcp:
- reference_reactions_validation.json: FacePASS, Hand/CheerFAIL, exported=false.
- reference_reactions_first_pass.json: история PASS, не актуальная версия.
- reference_pca_diagnosis.json, reference_pca_channels_rank4/6/8.json,
  latest_reference_validation_worst.json, reference_corrective_diagnosis.json.
- MCP diagnose wrappers в evidence04; traces evidence/blender_mcp.
- Наframe31 shape.value иcurve.evaluate совпадают, signedvalues сохранены.
  Сравнить worstframe19.5/38.5: evaluatedshapes, weightedrestsum, relative_key,
  mute/vertex_group, slot/action ownership, stale neutral wrist/finger channels.
- Найден stale sourceShapes action риск: .new создаёт .001/.002, set_clip берёт
  unsuffixed prefix. Записывать фактическое имя возвращённого action+slots в report.
  Это подозрение, не установленная причина45ммошибки.

FBX Gratia_Game_reference_preview.fbx и ReactFace/Hand/Cheer.fbx пока перваяверсия.
Финальный экспорт только MCP после≤1мм integer/half QA старых5+новых3, root/feet0,
визуальной проверки и новыхSHA. import_reference_reactions.py готов (py_compilePASS),
НЕ запускался: guardsreport/hashes, same skeleton/boneorder/materialslots/physics,
source+old morph subset, signedPCA curves+face; обновляет только routing/morphcount/
authoredfaceflag. Проверить Python reflection при первом запуске, guards не отключать.
Morphcount брать фактический, он может измениться после исправленияPCA.

## Сборки/проверки

- Editor C++ latestPASS evidence04/editor_reference_build_retry.log.
- Ранее math/tracking2testsPASS evidence04/automation/index.json.
- Последний automation_reference.log: filter Gratia. matched0; повторить Gratia
  или точныеимена, не считать0testsPASS.
- Ранее editor_game_test.log PASS smoke/selftest/W→cookedmapping→boundaction→Pawn,
  до последних input/response/presenter изменений.
- UATpackageSUCCESS затем finalfingerprintFAIL (Scriptschanged) в build_orchestrator.log.
  Packaged manifest отсутствует; текущийexe не подтверждённый актуальныйрелиз.
- После стабильных source/assets новый Build-Stage1.ps1; Test-Stage1,
  Test-CharacterProfiles (Gratia246bones vsManny89), poses/views/reactions.
- Test-CharacterReactions.ps1:6runs3cues×Front/Face, syntheticcontact→event→serial→
  actualclip, cooked/evaluatedface/correctivecurves, caption/reset/screenshots.
  Это не реальныйVRcontact/solver acceptance. Verify-Project -VisualChecks включает его.
- 15min mixedsoak ещё не выполнен. Реальные handfit/hold/левыйстик/90FPS не приняты.
  После desktopPASS запустить -vr безDemo, пользователь двигаетстик/руки,
  сохранять buildid+logs+егонаблюдение; не объявлять успех по demo hand=-1.

## Продолжение

Агенты завершились сообщением сервиса об исчерпании workspacecredits; вычисления
не продолжаются в фоне. Не ждать старыеagents, проверить процессы/файлы заново.
После работы обновлять README/docs/STATUS/MVP и meaningful Gitcommits.
