---
name: unravel-triage
description: >-
  Routes UnravelEngine tasks to the correct domain skill and identifies touch
  points across engine, editor, assets, and scripting. Use at the start of any
  non-trivial task, feature request, bug report, or refactor in UnravelEngine.
---

# Unravel Triage

Classify the task before writing code. Read relevant source first; never guess architecture.

## Step 1: Classify the domain

| Domain | Trigger signals | Primary skill |
|--------|-----------------|---------------|
| ECS / entities | component, system, scene, entity, serialize entity | `unravel-ecs-component` |
| Rendering | shader, pass, pipeline, bgfx, material, post-process, GPU | `unravel-rendering` |
| Editor UI | panel, inspector, gizmo, menu bar, ImGui, dock, undo | `unravel-editor-panel` |
| Assets | import, reimport, `.meta`, uid, asset compiler, handle | `unravel-assets` |
| Materials (MCP / `.mat`) | PBR material, `.mat`, material instance, `materials_set`, albedo/roughness maps | `unravel-materials` |
| Projects / scenes (MCP) | open project, recent projects, `.spfb`, scene preset, hub open | `unravel-projects` |
| Entities / transforms (MCP) | create primitive, parent, local vs world transform, scene_save, ScriptComponent MCP | `unravel-entities` |
| Viewport camera (MCP) | scene panel camera, focus entities, look_at, orbit, viewport screenshot | `unravel-viewport` |
| Scripting | C#, CoreCLR/dotnetpp, hot-reload, ScriptComponent, glue | `unravel-scripting` |
| Physics | collision, Bullet, rigidbody, character controller | `unravel-physics` |
| Animation | skeletal, blend space, clip, bone | `unravel-animation` |
| Game UI | RmlUi, HTML, CSS, ui_document | `unravel-ui-rmlui` |
| Audio | OpenAL, audio source, listener | `unravel-audio` |
| Prefabs | prefab override, instance, template | `unravel-prefabs` |
| Profiler | GPU timeline, eviction, frame stats | `unravel-profiler-debug` |
| Build / CI | CMake, workflow, .NET SDK, CPack | `unravel-build-verify` |
| Play mode | splash, play state, on_play_begin, simulation gating | `unravel-play-mode-change` |

If multiple domains apply, list all and identify the **primary** change surface.

## Step 2: Locate entry points

| Layer | Path |
|-------|------|
| Engine bootstrap | `engine/engine/engine.cpp` |
| Service locator | `engine/core/context/` (`rtti::context`) |
| Events | `engine/engine/events.h` |
| Play mode state | `engine/engine/play_mode.h` |
| Splash screen | `engine/engine/splash_screen.h` |
| ECS core | `engine/engine/ecs/` |
| Meta / reflection | `engine/engine/meta/` |
| Editor hub | `editor/editor/hub/` |
| Editor entry | `editor/editor/editor.h` |
| Game runner | `game/` |
| Shipped data | `engine_data/`, `editor_data/` |
| Build | `CMakeLists.txt`, `cmake/` |

## Step 3: Assess cross-cutting impact

Check each before implementing:

- [ ] **Serialization** — ser20 SAVE/LOAD + `all_serializeable_components` in `engine/engine/meta/ecs/components/all_components.h`
- [ ] **Meta / inspector** — `REFLECT()` in `engine/engine/meta/`, inspector in `editor/editor/hub/panels/inspector_panel/inspectors/`
- [ ] **Play mode** — `play_mode` phases: splash vs running; `on_play_begin` only in running phase
- [ ] **Prefab overrides** — `serialization::path_context` for per-instance property paths
- [ ] **C# API parity** — `engine_data/data/scripts/` if exposed to scripts
- [ ] **Asset pipeline** — `.meta` uid stability, importers in `engine/engine/assets/impl/importers/`
- [ ] **Editor-only vs runtime** — code in `editor/` must not be required at game runtime

## Step 4: Pick workflow skill

| Task shape | Workflow skill |
|------------|----------------|
| New component end-to-end | `unravel-add-component` |
| New render pass | `unravel-add-render-pass` |
| Shader edit | `unravel-shader-change` |
| Custom inspector | `unravel-add-inspector` |
| Play mode behavior | `unravel-play-mode-change` |
| Bug with repro | `unravel-bug-investigation` |
| Large / architectural | `unravel-architect` |

## Step 5: Plan output

Before coding, state:

1. Domain(s) and skill(s) to apply
2. Files to read (minimum set)
3. Files likely to change
4. Verification steps (build, manual test, play mode)

## Hard rules

- Do not modify `deps/3rdparty/` unless no alternative exists
- Match existing file naming: `snake_case` for types, files, functions
- Minimal diff — only touch what the task requires
- Follow `AGENTS.md` (C++ guidelines section) for all C++ changes

## System init order (reference)

From `engine/engine/engine.cpp`:

`threader` → `renderer` → `audio_system` → `asset_manager` → `ecs` → `rendering_system` → `transform_system` → `camera_system` → `reflection_probe_system` → `skylight_system` → `model_system` → `animation_system` → `physics_system` → `particle_system` → `input_system` → `script_system` → `ui_system`

New systems must respect this ordering and event hook priorities.
