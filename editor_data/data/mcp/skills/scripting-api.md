---
name: scripting-api
title: C# API reference - where it lives and how to read it
description: The engine's C# API is shipped as readable source under engine:/data/scripts - how to find a type, the four sample components to copy from, and the language rules (lifecycle, Entity is a struct, asset loading, RCSS) that compile errors would otherwise teach you the slow way. Read before writing any script.
order: 65
include: editor:/data/project/AGENTS.template
---
# The C# API is readable source, not a black box

Every type a script can use ships as C# source under `engine:/data/scripts`, and
`scripts_get_source` reads it by protocol path. Read the real declaration instead of
guessing a signature - this engine is not Unity and the near-miss APIs differ.

```
scripts_get_source {"path":"engine:/data/scripts/input/Input.cs"}
```

## Where things are

| Path under `engine:/data/scripts/` | Holds |
|---|---|
| `input/` | `Input` (action-name based), `KeyCode` |
| `scene/` | `Entity`, `Scene`, `Prefab`, `Components`, `LayerMask`, `EntityPool` |
| `scene/components/` | one file per component wrapper - `TransformComponent`, `CharacterControllerComponent`, `PhysicsComponent`, `CameraComponent`, `LightComponent`, `AnimationComponent`, `AudioSourceComponent`, `TextComponent`, `UIDocumentComponent`, ... |
| `scene/physics/` | `Physics` (raycasts), `Ray`, `RaycastHit`, `Collision`, `ContactPoint`, `ForceMode`, `RigidbodyType` |
| `math/` | `Vector2/3/4`, `Quaternion`, `Mathf`, `Color`, `Bounds`, `Random` |
| `assets/` | `Assets`, `Asset` |
| `audio/` | `Audio`, `AudioBus` |
| `ui/` | `UIDocument`, `UIElement`, `UIEventManager`, and the event types |
| `system/` | `Application`, `Screen`, `Attributes`, `Profiler`, `Object` |
| `logger/`, `gizmos/`, `kinematics/` | `Logger`, `Gizmos`, `Kinematics` |

## Start from a sample, not a blank file

`engine:/data/scripts/samples/` holds four working components. They are the fastest
way to see the idioms in context:

- `SampleCharacterController.cs` - movement driven by `Input` actions through
  `CharacterControllerComponent`
- `SampleMouseLookComponent.cs` - mouse-delta look
- `SampleOrbitComponent.cs` - transform animation over time
- `SampleUIWrapperController.cs` - driving a `UIDocument` from script

Read the one closest to the task before writing anything.

## Input is action names, not key codes

`Input.GetAnalogValue("Horizontal")`, `Input.IsPressed("Jump")` and friends resolve
*action names* defined in the project's input settings, not keys. The shipped default
mapping provides `Horizontal`, `Vertical`, `Jump`, `Run`, `Submit`, `Cancel`,
`Mouse X`, `Mouse Y`, `Mouse ScrollWheel`, `Mouse Left`, `Mouse Right`,
`Mouse Middle`. Use those names, or add bindings, rather than inventing a name that
is mapped to nothing - an unmapped action silently reads zero.

---

The rules below ship with every project as `UNRAVEL-AGENTS.md`. They are the same
rules a file-level agent working in the project directory follows, and they are
authoritative for language and asset semantics.
