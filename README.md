# GratiaVR — как устроен проект

PC VR игра на Unreal Engine 5.8.3 (OpenXR / SteamVR). Персонаж Gratia с мягким телом и
физикой волос; лобби и сцены в стиле ViRo Playspace, музыка управляет светом и окружением.
Что сделано и что проверено — [MVP.md](MVP.md) и [docs/STATUS.md](docs/STATUS.md).
Правила работы в репозитории — [AGENTS.md](AGENTS.md).

## Быстрый старт

| Задача | Что сделать |
|---|---|
| Открыть в редакторе | **Open-Editor.cmd**: компилирует C++ и открывает проект. Можно и двойным щелчком по `GratiaVR/GratiaVR.uproject`, если C++ не менялся |
| Открыть код | **Open-Code.cmd** → Visual Studio 2022 (см. «Работа с кодом») |
| Играть в редакторе на мониторе | Кнопка **Play** (Alt+P). Сразу открывается лобби с меню. Стрелки и Enter — меню, F4 — открыть/закрыть меню, WASD — ходьба |
| Играть в редакторе в шлеме | Запустить SteamVR, затем **Play ▸ VR Preview** |
| Собрать игру | Закрыть редактор и игру, запустить **Build.cmd**. Результат: `Builds/Windows`, единственная папка со сборкой |
| Запустить сборку | **Start-VR.cmd** (шлем) или **Start-Desktop.cmd** (монитор) |

Управление в шлеме — таблица в [GratiaVR/README.md](GratiaVR/README.md#запуск-и-управление).

## Папки

```
ue proto/
├─ GratiaVR/                 проект Unreal (единственный рабочий)
│  ├─ Content/               ассеты, см. ниже
│  ├─ Source/GratiaVR/       C++ игры
│  ├─ Source/GratiaVREditorTools/   C++ только для редактора (генерация физики, шрифтов)
│  ├─ Config/                настройки: карта по умолчанию, ввод, VR, качество
│  ├─ Scripts/               сборка, тесты и генераторы контента (PowerShell / Python)
│  └─ Plugins/KawaiiPhysics/ пружинная физика волос, ушей, хвоста и мягких частей
├─ Builds/Windows/           готовая игра (создаёт Build.cmd), build_manifest.json — что именно собрано
├─ Exports/                  исходники для импорта: Gratia_mvp.blend, FBX, анимации, аудио, шрифты
├─ docs/                     документация (список ниже)
├─ evidence/                 логи и снимки проверок; не в git
├─ Archive/ProjectCopies/    две старые копии проекта после перехода на 5.8; не используются, не в git
├─ Gratia.blend, Gratia_source.blend, Gratia_working.blend   исходная модель; не изменять
└─ Open-Editor.cmd, Open-Code.cmd, Build.cmd, Start-VR.cmd, Start-Desktop.cmd, Start-BlenderMCP.cmd
```

Служебные папки Unreal (`Binaries`, `Intermediate`, `Saved`, `DerivedDataCache`) создаются сами
и не хранятся в git.

## Content: что где

**Карта одна — `Gratia/Maps/L_Stage1`.** Она открывается в редакторе и в игре. В ней:

- актёр **Stage1Runtime** — VR-руки, трекинг, ходьба. Его компоненты: `WorldMenu` — меню,
  `SceneDirector` — лобби, загрузка, сцены и музыка;
- **персонаж** (`GratiaPreviewCharacter`) с профилем `DA_Gratia`;
- **комната-студия** — объекты с тегом `GratiaStudio`; она скрыта, пока открыто окружение.

| Папка / ассет | Что это |
|---|---|
| `Characters/Profiles/DA_Gratia` | Всё о персонаже: меш, клипы, зоны касаний, мягкое тело, пружины волос, реакции, перформансы (KM466). `DA_Mannequin` — проверочный профиль на Manny |
| `Gratia/GameRig` | Игровой меш `SK_Gratia_Game` и его анимации (Idle, KM466) |
| `Gratia/Character`, `Gratia/Animations` | Первый (preview) импорт модели |
| `Gratia/CharacterMaterials`, `Gratia/Textures` | Материалы и текстуры персонажа |
| `Gratia/Physics` | PhysicsAsset и параметры вмятины на коже |
| `Gratia/Input` | Input Actions игры (захват, меню, поворот) |
| `Gratia/Performance` | Музыка перформанса KM466 |
| `Gratia/Experience/DA_SceneLibrary` | Список сцен (название, окружение, трек, акустика, картинка), плейлист, музыка лобби, оформление меню |
| `Gratia/Experience/Environments` | Уровни сцен `L_WabiSabi`, `L_SoulCity` (маркеры, колонки, свет под музыку) и `Fab/L_WabiSabi_Art` (арт-уровень с запечённым светом); во время игры подгружаются в `L_Stage1` вместе с декорациями |
| `Wabi_Sabi_Interior`, `SoulCity` | Паки Fab (только локально, не в git; установка — [docs/EXPERIENCE.md](docs/EXPERIENCE.md#подготовка-контента)) |
| `Gratia/Experience/Materials`, `Music`, `Font`, `Previews` | Материалы окружений и меню, сгенерированные треки, шрифт Nunito, картинки карточек |
| `Gratia/UserMusic` | Ваши треки (только локально, не в git) |
| `XRFramework`, `XRMannequins`, `Characters/Mannequins` | Из шаблона Epic VR. Используются: VR-pawn, ввод контроллеров, модели рук, Manny |
| `Weapons`, `LevelPrototyping`, `VRSpectator`, `XRFramework/Levels` | Остатки шаблона. Игра их не использует (проверено по ссылкам), их можно удалить |

## Как менять руками

- **Персонаж** — `DA_Gratia` в Details. Настройки разбиты по категориям: Soft Body, Spring Chain,
  Contact, Secondary Motion, Performance, Hand Physics и другие. Подробно — [docs/CHARACTER_PROFILE.md](docs/CHARACTER_PROFILE.md).
- **Окружение** — открыть `Experience/Environments/L_…`, менять, сохранять. Чтобы увидеть его
  с персонажем: открыть `L_Stage1`, Play и выбрать сцену в меню. Смысл тегов (свет и объекты,
  которые реагируют на музыку, места персонажа, игрока и колонок) — в [docs/EXPERIENCE.md](docs/EXPERIENCE.md#теги-окружений).
- **Сцены, треки, порядок карточек** — `DA_SceneLibrary` в Details.
- **Меню и экраны загрузки** — C++: `GratiaMenuWidget.cpp`, `GratiaLoadingWidget.cpp`.
- **Логика** — C++ в `GratiaVR/Source/GratiaVR`. Какой класс за что отвечает —
  [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#где-менять-поведение). После правки C++ закрыть редактор
  и запустить Open-Editor.cmd.

## Работа с кодом (C++)

1. **Open-Code.cmd** создаёт проектные файлы и открывает `GratiaVR/GratiaVR.sln` в Visual Studio 2022.
   После добавления или удаления .cpp/.h запустить его снова.
2. В Visual Studio выбрать конфигурацию **Development Editor**, платформу **Win64** и стартовый
   проект **GratiaVR**. F5 запускает редактор под отладчиком: точки останова срабатывают в Play.
3. **Мелкие правки .cpp при открытом редакторе** — Live Coding: сохранить файл и нажать
   Ctrl+Alt+F11 в редакторе. Изменения в .h, `UPROPERTY`/`UFUNCTION` и новые файлы требуют
   закрыть редактор и собрать заново (F5 в VS или Open-Editor.cmd).

| Код (`GratiaVR/Source/GratiaVR`) | За что отвечает |
|---|---|
| `GratiaStage1Runtime` | Главный актёр карты: VR-руки, трекинг, вибрация; владеет меню, директором сцен, ходьбой и вводом |
| `GratiaPreviewCharacter`, `GratiaCharacterProfile` | Персонаж и его данные (Data Asset `DA_Gratia`) |
| `GratiaAnimInstance`, `GratiaHandAnimInstance` | Анимация тела (клипы, взгляд, пружины KawaiiPhysics) и пальцев рук |
| `GratiaInteraction`, `GratiaContactSolver`, `GratiaBodySurface` | Касания: зоны, коллизии руки с телом, выбор реакции |
| `GratiaSoftBodyInteraction`, `GratiaSecondaryMotion` | Мягкое тело: нажим, сжатие, захват, пряди волос |
| `GratiaPenetrator`, `GratiaPenetration`, `GratiaPenetrationMath` | Суставной примитив (размеры S–XXL) и каналы тела: захват у входа, суставы по каналу, кости стенок, реакции, вибрация |
| `GratiaReactionPresentation` | Звук и подписи реакций |
| `GratiaPerformanceStage` | Перформансы (KM466): музыка, партнёр, вид его глазами |
| `GratiaMenu`, `GratiaMenuWidget` | Меню в пространстве (логика и вид) |
| `GratiaSceneDirector`, `GratiaSceneLibrary`, `GratiaMusicPlayer`, `GratiaLoadingSpace`, `GratiaLoadingWidget` | Лобби → загрузка → сцена, музыка и переходы, экран загрузки |
| `GratiaLocomotion`, `GratiaHandInput` | Ходьба, поворот, триггеры и grip |
| `*Verification` | Автопроверки; работают только с флагами запуска (`-GratiaFlowQA` и др.) |
| `Tests/` | Automation-тесты: Tools ▸ Session Frontend ▸ Automation, фильтр `Gratia` |

`Source/GratiaVREditorTools` — код только для редактора: генерация физики, шрифтов, перенос ткани.
Правила для кода (без имён костей Gratia в общей логике, данные — в профиле) — [AGENTS.md](AGENTS.md).

**Что создано скриптами.** Окружения, их материалы, `DA_SceneLibrary`, шрифт и музыку создал
`Scripts/setup_scene_experience.py`. Сборка его не запускает. Если запустить его (`Build.cmd -RegenerateScenes`),
он пересоздаст объекты `Experience_*` в окружениях, материалы и библиотеку сцен, и ручные правки
в них пропадут. Объекты, которые вы добавили сами под другими именами, он не трогает.

## Документация

| Файл | О чём |
|---|---|
| [MVP.md](MVP.md) | Цели, что принято, чек-лист проверки в шлеме |
| [docs/STATUS.md](docs/STATUS.md) | Текущая сборка и результаты всех проверок |
| [BUILDING.md](BUILDING.md) | Сборка и запуск подробно |
| [docs/EXPERIENCE.md](docs/EXPERIENCE.md) | Лобби, сцены, музыка, теги окружений |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | C++ классы и где менять поведение |
| [docs/CHARACTER_PROFILE.md](docs/CHARACTER_PROFILE.md) | Профиль персонажа и замена модели |
| [docs/HAND_PHYSICS.md](docs/HAND_PHYSICS.md) | Руки, мягкое тело, захват, вибрация от касаний |
| [docs/MOCAP_KM466.md](docs/MOCAP_KM466.md) | Перенос мокапа KM466 |
| [GratiaVR/Scripts/README.md](GratiaVR/Scripts/README.md) | Скрипты подготовки, импорта и тестов |

## Известные мелочи редактора

- Если окно редактора не в фокусе, Unreal снижает частоту до 3 кадров/с. Тогда в Output Log
  идут предупреждения `PERF_SPIKE`; это не проблема игры.
- При первом открытии Unreal может предложить обновить файл проекта или включить плагины.
  Обновление уже внесено. Плагины OpenXREyeTracker и OpenXRHandTracking не нужны: игра
  проверена без них.
