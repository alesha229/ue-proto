# GratiaVR: архитектура и сопровождение

Правила всех последующих работ находятся в корневом [AGENTS.md](../AGENTS.md).
Эта схема описывает рабочее устройство проекта. Перенос native Chaos Cloth
5 октября 2026 реализован; editor QA PASS, результаты пакета — в STATUS.md.

Мягкая поверхность тела и связанной одежды получает отдельный путь:
`GratiaClothPortLibrary` переносит три включённые исходные клетки Blender и маски
SurfaceDeform в mesh-owned Chaos Clothing Asset. `GratiaClothInteraction` работает
с его частицами и коллизиями рук; `GratiaClothVerification` проверяет частицы и
деформацию видимых вершин только при `-GratiaClothQA`. Тело группы 3 отключается
в rigid-body симуляции, чтобы одна поверхность не симулировалась дважды.

Дополнение: SecondaryMotion принимает разрешённые отсчёты обеих видимых рук и
на следующем PrePhysics применяет ограниченное давление через sweep геометрии
активных Chaos bodies. Это физический путь, независимый от ContactReaction.
Настройки/ограничения/проверки: [HAND_PHYSICS.md](HAND_PHYSICS.md).
HandInput отдельно читает Enhanced Input actions триггеров. SecondaryMotion
захватывает ближайшее разрешённое тело ограниченной пружиной; Runtime передаёт
разрешение только после tracking/recovery/solver gates и при закрытом меню.
Результаты конкретных сборок записываются отдельно; наличие компонента не подтверждает VR-приёмку.

```mermaid
flowchart LR
    XR[OpenXR / controller keys] --> EI[Enhanced Input / bound actions]
    EI --> Move[Locomotion / body sweep]
    EI --> Grab[HandInput / trigger actions]
    Grab --> Physics
    Grab --> Cloth[ClothInteraction / native Chaos cloth]
    Solver -->|allowed visual hand| Cloth
    Solver -->|allowed visual hand| Physics
    Move --> Pawn[XR Pawn]
    XR --> Track[Runtime / tracking gates]
    Track --> Solver[Independent contact solver]
    Solver --> Contact[Interaction / zones / state / events]
    Profile[CharacterProfile Data Asset] --> Contact
    Profile --> Anim[Native animation or custom AnimBlueprint]
    Profile --> Physics[SecondaryMotion / driven Chaos]
    Profile --> Cloth
    Contact --> Anim
    Contact -->|OnContactReaction / OnContactReset| Present[ReactionPresentation / local sound / captions]
    Profile --> Present
    Menu[World menu] --> Move
    Menu --> Contact
    Verify[RuntimeVerification / explicit test flags] -.-> Track
```

## Где менять поведение

- `GratiaLocomotion`: привязанный InputComponent, движение относительно взгляда,
  sweep корпуса и поворот вокруг камеры. Скорость, dead zone и размеры корпуса доступны в Details.
- `GratiaStage1Runtime`: калибровка, устойчивость трекинга, восстановление и визуальные руки.
  `TargetCharacter` и `SceneContactActor` задаются в карте или через API; случайный первый персонаж не выбирается.
- `GratiaInteraction`: hover/touch/hold/cooldown, владелец руки, выбор реакции,
  событие `OnContactReaction`. Контактные зоны и collision proxies задаются раздельно.
- `GratiaReactionPresentation`: явно принадлежит хосту персонажа и подписан на события
  взаимодействия. Показывает временную подпись и воспроизводит звук; контакты не создают
  компоненты текста или звуковые ресурсы. `OnContactReset` очищает подпись и останавливает
  звук при сбросе/смене профиля; подписки удаляются в EndPlay.
- `GratiaContactSolver`: sweep/depenetration для сфер, капсул и AABB; не знает модели или карты.
- `GratiaPreviewCharacter`: хост выбранного профиля, клипы/мигание и Blueprint getters.
  Имя класса сохранено для совместимости существующей карты; он принимает разные скелеты.
- `GratiaAnimInstance`: маленький native proxy для проверенных клипов, взгляда и пружин.
- `GratiaSecondaryMotion`: ограниченная симуляция групп, бюджеты, физические приводы и аварийный сброс.
- `GratiaHandInput`: отдельные bound actions двух триггеров, готовность контекста,
  значения для захвата и диагностика. Захват аксессуаров принадлежит SecondaryMotion,
  мягкой поверхности — ClothInteraction.
- `GratiaClothInteraction`: отдельный PrePhysics-путь двух рук к native cloth,
  ограниченный хват динамической частицы, сброс, диагностика и контроль отклонения.
  Обновление завершённой симуляции предшествует следующему mesh/cloth tick.
- `GratiaSourceClothingAsset`: подкласс Chaos clothing asset; хранит editor-only
  привязку render-секций к source-клеткам и восстанавливает её при штатной
  перепривязке Unreal во время пересборки меша/cook. В игре — обычный cloth asset.
- `GratiaClothVerification`: отдельная opt-in QA мягкой поверхности; synthetic
  нажим/хват, наблюдение частиц и видимых вершин, отпускание и tracking recovery.
- `GratiaMenu`: скрытое по умолчанию меню в пространстве; получает цель от Runtime.
- `GratiaRuntimeVerification`: отдельное состояние smoke/self-test, synthetic-input test,
  soak и метрики. Оно выполняется только при соответствующих аргументах запуска.
  В soak только тест владеет контактными отсчётами: обычные припаркованные desktop-руки
  не обновляют их и не продлевают correction recovery синтетического контакта.
- `GratiaVREditorTools`: генерация PhysicsAsset и перенос source cloth только
  в редакторе; модуль отсутствует в игре.

## Модель и ручная работа

`/Game/Characters/Profiles/DA_Gratia` хранит mesh, PhysicsAsset, базовые клипы и таблицу реакций по зонам,
semantic bone/morph maps, возможности, зоны/прокси, таймеры, настройки взгляда,
бюджеты, приводы и пружины. У другого профиля отсутствующие возможности отключаются явно.
Runtime не меняет общий Data Asset. Подробный порядок замены — [CHARACTER_PROFILE.md](CHARACTER_PROFILE.md).

Для звука профиль содержит необязательные `ReactionSounds` (имя зоны → ресурс, затем
ключ `Default`) и `DefaultReactionSound`. Если ресурс не назначен, presenter использует
короткий процедурный сигнал. Назначенные звуки должны быть конечными и не зацикленными.
Выключатель `Interaction.bSound`, capability профиля и cooldown сохраняются; параметры
подписи берутся из `ContactSettings`. Presenter можно отключить для своего слушателя
`OnContactReaction` без изменения определения касаний.

Клипы, mesh и PhysicsAsset редактируются стандартными редакторами Unreal.
Профиль обновляет mesh хоста при назначении в Details. Для собственного Animation Blueprint
предусмотрены `AnimationClass` и `GetCharacterAnimationSnapshot` в Event Blueprint Update Animation.
Текущая анимация Gratia остаётся native SingleNode proxy: её полноценный перевод на редактируемый
AnimGraph с отдельными skeletal-control узлами — дополнительная работа, а не выполненная миграция.

Подготовка модели/экспорт в Blender выполняются через Blender MCP.
PowerShell/Python автоматизируют разработку, импорт, сборку и проверки; готовая игра от них не зависит.

Cloth переносит исходную топологию, bone weights, Pin и область влияния, но солвер
Chaos отличается от Blender. Активные cloth-вершины подавляют обычное позиционное
морф-смещение; лицо и остальные исключённые вершины сохраняют skinning/морфы.
Пересекающиеся SurfaceDeform-маски используют ближайшую допустимую клетку вместо
последовательной композиции Blender. Исходные hair SurfaceDeform выключены в render
и не активируются переносом. Детали и пределы QA: [HAND_PHYSICS.md](HAND_PHYSICS.md).

## Git и сборки

Git хранит исходники/настройки/документы. Git LFS хранит Unreal assets, Blender и изображения.
Первый коммит сохраняет предыдущее состояние. Remote/push не настроены этой работой.
Для клона нужны Git LFS и `git lfs pull`; движок задаётся параметром `-EngineRoot`.

`GratiaVR/Scripts/Build-Stage1.ps1` создаёт build stamp перед компиляцией и manifest
`Builds/Stage1/Windows/build_manifest.json`: commit/dirty state, engine, UTC,
SHA-256 исходников/настроек/скриптов/ассетов и итогового exe. После упаковки
проверяется, что входные файлы не изменились и каждый архивированный файл совпадает
со staged-файлом по SHA-256; список записан в `package_files`. Build id виден в логе и F1.
Прямой Build.bat подходит для разработки; versioned package выпускается через Build-Stage1.

## Проверки

1. `Gratia.Math.ContactSolver`, `Gratia.Math.HandPressure` и `Gratia.Stage1.TrackingLossGate`:
   независимые automation tests (фильтр `Automation RunTests Gratia`, ожидаются три теста).
2. `Test-Stage1.ps1`: три запуска готовой сборки; lifecycle, профиль, planted limits,
   физические бюджеты, геометрия и keyboard key → mapping → bound action → Pawn delta.
   Также проверяются размеры коллайдеров, передача вращения четырёх физических групп
   видимым костям, Z/X → grab actions → разные тела, отпускание и tracking reset.
3. `Test-CharacterProfiles.ps1`: настоящие Gratia/Manny на разных скелетах, с явными skips недоступных возможностей.
4. `Test-CharacterPoses.ps1` / `Test-CharacterViews.ps1` / `Test-CharacterReactions.ps1`:
   клипы, cooked/evaluated morph curves и снимки готовой игры.
5. `-GratiaSoakSeconds=900`: длительный synthetic mixed soak; учитывает реальные срабатывания
   внутри синтетического сценария, не выдаёт номер целевой зоны за доказательство её покрытия.
6. Реальный VR: левый стик, tracking/contact/menu и SteamVR frame delivery проверяются устройствами.

F1 показывает raw/mapped stick, Pawn delta и причину блокировки. `MOVEMENT` в логе
различает missing player/action/context, zero input, menu blocked, collision blocked и moving.
Контактная диагностика показывает tracking/recovery/correction gate и ближайшую зону.
`source=demo` / `hand=-1` не доказывают реальные касания.

Нужны отдельные аппаратные проверки после любой смены OpenXR bindings или геометрии прокси.
CI можно добавить при выборе хоста с установленным Unreal; локальные проверки уже воспроизводимы.
