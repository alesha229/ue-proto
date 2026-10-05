# Сборка и запуск GratiaVR

Рабочий проект: `GratiaVR/GratiaVR.uproject`. Движок по умолчанию: UE 5.8.3 в `E:\ue\UE_5.8`.

1. Закрыть игру и Unreal Editor, запустить **Build.cmd** в этой папке. Он вызывает единственный активный pipeline `GratiaVR/Scripts/Build-Stage1.ps1`.
2. Для VR сначала запустить SteamVR и подключить шлем с двумя контроллерами, затем **Start-VR.cmd**.
3. Для настольного запуска использовать **Start-Desktop.cmd**.

Единственная папка готового пакета: **Builds/Windows**. Каждый успешный Build.cmd обновляет её. Перед запуском ярлыки проверяют SHA256 исполняемого файла и показывают build id из `build_manifest.json`.

Команды из терминала:

```cmd
Build.cmd
Build.cmd -EngineRoot "D:\Unreal\UE_5.8"
Start-VR.cmd
Start-Desktop.cmd
```

Дополнительные параметры передаются скрипту сборки или игре. Для сборки без паузы в терминале предварительно выполнить `set GRATIA_NO_PAUSE=1`.

Логи сборки: `evidence/04/editor_build.log`, `evidence/04/package.log`. `GratiaVR/Saved/StagedBuilds` — промежуточные данные pipeline. Прежние копии проекта сохранены в `Archive/ProjectCopies`; рабочая папка одна — `GratiaVR`.
