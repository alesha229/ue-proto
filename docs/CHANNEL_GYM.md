# Полигон каналов (channel gym)

Быстрая настройка и проверка каналов без перезапусков и без сборки пакета.

## Запуск

`Gym-Channels.cmd` в корне (или вручную):

```
UnrealEditor.exe GratiaVR.uproject -game -nohmd -windowed -ResX=1600 -ResY=900 -GratiaChannelShots -GratiaChannelGym
```

Первый проход снимает все случаи: пусто, три пальца, ладонь, кулак, две руки, XXL, глубокий 4XL (живот до и после),
бусины XL, узел L, зияние после XXL (через 0,3, 3 и 10 с), маленькое после большого. Снимки — в
`GratiaVR/Saved/Screenshots/ChannelShots`: вдоль оси, сбоку, сзади, живот. Затем игра остаётся открытой и ждёт
изменений в `GratiaVR/Saved/ChannelGym/gym.json`.

## gym.json

```json
{
  "serial": "7",
  "cases": ["Gape", "SmallAfterBig"],
  "channels": ["Anal"],
  "settings": { "GapeShare": 0.9, "CloseDelaySeconds": 3.0, "ShaftDeformRangeCm": 3.0 },
  "channel": { "Anal": { "RestRadiusCm": 0.3, "MorphFullOpeningCm": 6.2 } },
  "ring": { "Anal/DEF-ass": [1.0, 1.2, 4.0] },
  "livecoding": false,
  "quit": false
}
```

- `cases`, `channels` — какие случаи снимать (пусто — все; `Forms` — ряд форм примитива).
- `settings` — поля `Penetration` профиля по имени: зияние, сопротивление, подгонка.
- `channel` — поля канала по имени канала.
- `ring` — кости стенок по префиксу имени: `[отклик, с какого раскрытия, максимум]`.
- `livecoding: true` — сначала собрать изменённый C++ через Live Coding и подменить код в запущенной игре. Подходят
  правки тел функций, без изменения заголовков и полей.
- `quit: true` — закрыть игру.

Проход заканчивается файлом `done.txt` (serial и номер прохода) и `report.txt` с диагностикой каждого снимка.
Значения применяются только к загруженному профилю; удачные переносятся в `setup_penetration.py` или в `DA_Gratia`.

Сетку и морфы меняет `setup_penetration.py`: около 80 с, одна сборка меша вместо тринадцати. После него полигон
перезапускают. Материал (`setup_shaft_material.py`) пересобирается, только если поменялся код шейдера.
