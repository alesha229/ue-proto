# Pipeline — phases and gates

Read by `studio.py status`. The current phase is the first one that has open gates or
unfilled `<placeholders>` in its files (`files:`). Gates are never deleted: they are
ticked (`studio.py tick`), reopened (`--reopen`), skipped (`--skip`) or
blocked (`--block`). Going back to any phase = reopening its gates.

Checks: `check: <command>` — status runs it itself and ticks the gate if it passes;
`check: manual` — the agent judges; `check: user` — only after an explicit "yes" from
the user (AskUserQuestion) + `studio.py decide --by user`. On autopilot —
`tick "…" --proxy` (PROXY mark), the user reviews it on return.

## Phase 0 — Start
files:
- [ ] project under git — check: git rev-parse --is-inside-work-tree SKIP: Existing project continuation; original MVP.md retains acceptance requirements.

## Phase 1 — Idea
files: CONCEPT.md
- [ ] interview done, concept slots filled with source tags — check: manual SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] 3–5 falsifiable pillars and anti-pillars — check: manual SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] story skeleton in STORY.md (or decided there is no story) — check: manual SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] draft mechanics in DESIGN.md and scale in SCOPE.md — check: manual SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] game designer proposed signature hooks, the user chose — check: user SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] user approved the pitch card — check: user SKIP: Existing project continuation; original MVP.md retains acceptance requirements.

## Phase 2 — Style
files: ART_BIBLE.md
- [ ] ≥3 directions with real references shown on the board — check: manual SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] user locked the style — check: user SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] art bible: hex palette, lighting, form, ≥3 signature techniques, "forbidden" — check: manual SKIP: Existing project continuation; original MVP.md retains acceptance requirements.

## Phase 3 — Platform and engine
files: TECH.md
- [ ] stack research done — check: test -s .studioigor/research/stack.md SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] user chose the platforms — check: user SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] user chose the engine; alternatives and reasons in TECH.md — check: user SKIP: Existing project continuation; original MVP.md retains acceptance requirements.

## Phase 4 — Environment
files: ENV.md
- [ ] research of the best engine setup done — check: test -s .studioigor/research/environment.md SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] full setup installed: engine, CLI, plugin/skills, MCP, tests, linter, export — check: python3 {skill}/scripts/envcheck.py --engine {engine} SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] editor MCP connected: `claude mcp list` ✔ and a real tool call succeeded — check: manual SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] hello project: TEST and CAPTURE pass (studio.py verify) — check: grep -q "^VERIFIED: yes" .studioigor/ENV.md SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] user saw the project running on their machine — check: user SKIP: Existing project continuation; original MVP.md retains acceptance requirements.

## Phase 5 — Assets and look-dev
files: ASSETS.md
- [ ] asset and pipeline research done — check: test -s .studioigor/research/assets.md SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] strategy by category agreed — check: user SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] look-dev scene built per the art bible and captured — check: ls .studioigor/captures/lookdev/*.png SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] user approved the look-dev: "this is how the game will look" — check: user SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] everything third-party is in the license registry — check: manual SKIP: Existing project continuation; original MVP.md retains acceptance requirements.

## Phase 6 — Mechanics
files:
- [ ] all mvp mechanics locked, each with research and a playtest — check: python3 {skill}/scripts/studio.py mechanics --check SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] design holism check passed, findings in BACKLOG.md — check: manual SKIP: Existing project continuation; original MVP.md retains acceptance requirements.

## Phase 7 — Vertical slice
files:
- [ ] full loop: menu → game → result → restart/next — check: manual SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] game-ness checklist passed for the vertical slice (references/production.md) — check: manual SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] post-slice boost ideas (synergies, new mechanics) shown and decided by the user — check: user SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] vertical slice with real assets, sound and story delivery — check: manual SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] user played through the vertical slice, it's fun; verdict recorded — check: user SKIP: Existing project continuation; original MVP.md retains acceptance requirements.

## Phase 8 — Content
files: SCOPE.md
- [ ] scope contract met — check: python3 {skill}/scripts/scope.py . SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] contact sheet of all units shown; neighbors differ on ≥2 axes — check: manual SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] story played from start to finale (if story-driven) — check: manual SKIP: Existing project continuation; original MVP.md retains acceptance requirements.
- [ ] user played the new content — check: user SKIP: Existing project continuation; original MVP.md retains acceptance requirements.

## Phase 9 — Polish
files:
- [ ] juice on the core actions, sound and motion on each — check: manual
- [ ] first 60 seconds: a newcomer starts playing without explanations — check: user
- [ ] performance budgets hold at the worst moment — check: manual
- [ ] user decided: the polish is good enough — check: user

## Phase 10 — Release
files:
- [ ] platform requirements research — check: ls .studioigor/research/release-*.md
- [ ] no stubs, debug, cheat keys, console.log — check: manual
- [ ] soak session with no leaks or degradation; saves compatible — check: manual
- [ ] end credits/CREDITS built from the license registry — check: manual
- [ ] release builds made for each platform — check: manual
- [ ] user decided whether and where to publish — check: user
