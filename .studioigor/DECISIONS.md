# Decisions

One line = one decision. `by`: user — the user decided; agent — the agent decided on its own
(the user chose "you decide" or the question wasn't worth their time). A cancelled
decision is not deleted: a new line says "replaces #N".

| # | date | phase | decision | options | by | why |
|---|---|---|---|---|---|---|
| 1 | 2026-10-06 | 0 | Продолжить текущий GratiaVR без смены движка и модели: закончить SceneDirector, меню, ресурсы и packaged QA. | — | user | Прямой запрос: привести игру в нормальный билд как viroplayspace и пройтись по MVP. |
| 2 | 2026-10-06 | 0 | Аудит кода: сохранить компоненты; исправить отсутствующую интеграцию Menu API, SceneDirector Runtime API и cooked SceneLibrary. | — | agent | Файлы Claude сохранены, но зависимости UMG и ресурсы Experience отсутствуют; это блокирует компиляцию и игровой flow. |
