# Unreal MCP для GratiaVR

Используется встроенный экспериментальный плагин Epic из установленного Unreal Engine 5.8.3. Сторонние серверы и плагины не скачивались.

В проекте включены `ModelContextProtocol` и `EditorToolset` только для Editor target. Их зависимости предоставляют ToolsetRegistry, Python и инструменты редактора. MCP не входит в самостоятельную игровую сборку.

Адрес: `http://127.0.0.1:8000/mcp`. В Config/DefaultEngine.ini задан `DefaultBindAddress=127.0.0.1`; доступ ограничен этим компьютером.

Config/DefaultEditorPerProjectUserSettings.ini включает автозапуск сервера и поиск инструментов. При ручном запуске редактора можно передать `-ModelContextProtocolStartServer -ModelContextProtocolPort=8000`.

## Подключение Codex

В общей конфигурации Codex добавлен сервер `unreal` через `codex mcp add unreal --url http://127.0.0.1:8000/mcp`. Существующие настройки других серверов не менялись.

После изменения конфигурации перезапустить подключение в Settings → MCP servers → unreal → Restart. Проверка записи: `codex mcp get unreal`. Доступные инструменты текущего чата зависят от обновления подключения; наличие записи не является проверкой живого сервера.

## Проверка сервера

`../evidence/mcp/verify_mcp.py` выполняет MCP initialize, tools/list и доступный вызов только чтением, сохраняя ответы. Запускать его после полной загрузки редактора. Порт должен слушать на 127.0.0.1; endpoint принадлежать процессу GratiaVR. Исходная попытка до загрузки редактора получила connection refused — результат не скрывается.

Живой сервер уже подтвердил MCP initialize с HTTP 200 и протоколом 2025-11-25, список инструментов и чтение текущего уровня `/Game/Gratia/Maps/L_Stage1`. Также проверены чтение состояния PIE и захват изображения редактора/сцены. Ответы и изображения находятся в `../evidence/mcp`; после перезапуска редактора нужна новая MCP-сессия.

При вызове `ActorTools.set_actor_transform` передавать location, rotation и scale целиком и проверять результат через get_actor_transform. В этой версии частичный transform с одним rotation сбросил location в ноль, несмотря на описание инструмента. Исправление и проверка сохранены в ответах 35–38.

При bEnableToolSearch=True наружу выставлены list_toolsets, describe_toolset и call_tool. Они позволяют находить инструменты работы с ассетами, актёрами и Blueprint. Встроенный EditorToolset содержит read_graph_dsl, write_graph_dsl и compile_blueprint; для многошаговых действий есть ProgrammaticToolset. Это ограниченная оркестрация зарегистрированных инструментов, а не произвольный Python import unreal.

## Источники

- [Epic: Unreal MCP](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-mcp-in-unreal-editor?application_version=5.8).
- [OpenAI: MCP configuration](https://learn.chatgpt.com/docs/extend/mcp?surface=cli).
- Точные возможности и настройки дополнительно сверены с исходниками установленного Engine/Plugins/Experimental/ModelContextProtocol и Toolsets/EditorToolset.
