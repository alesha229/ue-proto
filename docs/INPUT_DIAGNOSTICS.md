# Проверка VR-ввода и перемещения

## Найденная причина старой привязки

В старой v0.5 IA_Walk имел тип Axis2D, но был привязан отдельно к
OculusTouch_Left_Thumbstick_X и OculusTouch_Left_Thumbstick_Y.
Для Y добавлялся Swizzle. Это работало по правилам обычного Enhanced Input,
но не соответствовало способу, которым установленный Unreal 5.8 создаёт OpenXR actions.

OpenXRInput.cpp::ToActionType преобразует Axis2D в XR_ACTION_TYPE_VECTOR2F_INPUT.
SuggestBindingForKey преобразует ключи _X/_Y в скалярные OpenXR-пути
/input/thumbstick/x и /input/thumbstick/y. По спецификации OpenXR векторное
действие должно быть привязано к родительскому пути /input/thumbstick.
Для него используется ключ _2D, без Swizzle.

Поддерживаемые в установленном InputCore ключи нового IA_Walk:

- OculusTouch_Left_Thumbstick_2D
- ValveIndex_Left_Thumbstick_2D
- MixedReality_Left_Thumbstick_2D
- Vive_Left_Trackpad_2D

PICO не добавлен в эту настройку: соответствующие ключи/interaction profile
не зарегистрированы установленными InputCore/OpenXR-плагинами.
Добавлять его следует вместе с реально установленным и проверенным провайдером.

У Valve Index на левой руке кнопки называются A/B. Ключ
ValveIndex_Left_X_Click не является зарегистрированным ключом InputCore;
для предыдущего пункта меню используется ValveIndex_Left_A_Click.

Подготовительный скрипт удаляет из проектной копии IMC_Default только старые
IA_Move и IA_Turn/IA_Turn_* mappings, предварительно записывая action names
и удалённые keys в manifest. Руки, grab, меню и другие контексты сохраняются.

## Что означает диагностический raw

В установленном OpenXR Enhanced Input получает значение напрямую через
InjectInputForAction. Для этого пути SendControllerEvents не посылает
обычные события клавиш OculusTouch_*.
Поэтому PlayerController::GetInputAnalogKeyState для этих ключей может
оставаться нулём даже при корректном OpenXR-вводе.

Этот канал следует подписывать как legacy key state. Его ноль не доказывает
отсутствие ввода контроллера. Проверяемое значение для игры — bound IA_Walk,
а доказательство физического источника требует реального VR-сеанса без
синтетической инъекции и наблюдения движения стика.

## Проверка после исправления

1. Выполнить GratiaVR/Scripts/setup_gratia_locomotion.py в Unreal Editor.
   Скрипт проверяет сохранённые XR _2D mappings, отсутствие их modifiers,
   направления клавиш W/S/A/D и удаление старых template locomotion mappings.
2. Полностью пересобрать готовую игру. OpenXR фиксирует actions при создании
   сеанса; изменение mapping после его старта не заменяет attached action set.
3. Выполнить настольный self-test. Он посылает W через PlayerController::InputKey,
   проверяет реальное состояние клавиши, cooked mapping, bound IA_Walk,
   движение Pawn и остановку после отпускания. Это не тест OpenXR-контроллера.
4. Запустить конкретную новую сборку через SteamVR. Проверить отсутствие
   XR_ERROR_PATH_UNSUPPORTED при предложении bindings. Успешный старт сам по себе
   ещё не подтверждает движение.
5. В шлеме, с закрытым меню, отклонить левый стик вперёд/назад/в стороны,
   затем отпустить. В журнале MOVEMENT должны одновременно измениться
   mapped, pawn_delta_cm и reason=moving; после отпускания delta должен быть нулём.
6. Проверить остановку у стены и отдельный поворот правым стиком.
   Если mapped ненулевой, но delta равен нулю, исследовать collision result.
   Если enabled=0, исследовать состояние меню.

Журнал старого запуска evidence/04/user_v05_vr_retest.log содержит
XR_ERROR_PATH_UNSUPPORTED. Правая ось поворота получала значение,
а получение ненулевого левого вектора в старой версии не подтверждено.
Исправление mapping считается реализованным после обновления ассетов;
реальная ходьба считается проверенной только после пункта 5.

## Первичные источники

- Установленный движок:
  Engine/Plugins/Runtime/OpenXR/Source/OpenXRInput/Private/OpenXRInput.cpp
  (ToActionType, BuildEnhancedActions, SuggestBindingForKey, SendControllerEvents).
- Зарегистрированные ключи:
  Engine/Source/Runtime/InputCore/Private/InputCoreTypes.cpp.
- [Спецификация OpenXR: action bindings и преобразования типов](https://registry.khronos.org/OpenXR/specs/1.0-khr/html/xrspec.html#input).
