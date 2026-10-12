---
name: scripting-api
title: C# API reference - where it lives and how to read it
description: The engine's C# API is searchable (scripts_api_search / scripts_api_get) and shipped as readable source under engine:/data/scripts - how to find a type, the four sample components to copy from, and the language rules (lifecycle, Entity is a struct, asset loading, RCSS) that compile errors would otherwise teach you the slow way. Read before writing any script.
order: 65
include: editor:/data/project/AGENTS.template
---
# The C# API is readable source, not a black box

## Search it first

`scripts_api_search` finds types and members of the engine's API and of the project's
own scripts by words in their names and documentation; every word must match.
`scripts_api_get` lists a whole type with every public member's C# signature and
summary. Search before writing a helper - the API may already have it (screen and
camera conversions, entity pooling, audio buses, transform child lookup...).

```
scripts_api_search {"query":"camera screen ray"}
scripts_api_search {"query":"pool"}
scripts_api_get {"type":"CameraComponent"}
```

## Read the source for the details

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
| `math/` | `Vector2/3/4`, `Quaternion`, `Mathf`, `Color`, `Bounds`, `Random`, `Gradient`, `Curve`, `MinMaxCurve` |
| `assets/` | `Assets`, `Asset` |
| `audio/` | `Audio`, `AudioVoice`, `AudioBus` |
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

## Timing: coroutines and Invoke

Sequences over time are coroutines, not hand-written timers. `StartCoroutine` takes an
iterator; it can yield `null` (next frame), `new WaitForSeconds(s)` (game time),
`new WaitForSecondsRealtime(s)`, `new WaitForFrames(n)`, `new WaitUntil(() => ...)`,
`new WaitWhile(() => ...)`, another iterator (runs inline) or a `Coroutine` (waits for
it). `Invoke(delay, action)` and `InvokeRepeating(delay, interval, action)` are the
one-line forms. Coroutines run after the scripts' updates, pause with the game and stop
when the component is disabled or destroyed; `StopCoroutine` / `StopAllCoroutines`
end them earlier. `Time.unscaledDeltaTime` and `Time.realtimeSinceStartup` ignore the
time scale.

```csharp
IEnumerator Spin()
{
    yield return new WaitForSeconds(0.5f);
    yield return StartCoroutine(StopReels());   // wait for another coroutine
    Invoke(1.0f, ShowWin);
}
```

## Pooling

High-churn objects (coins, bullets, VFX) come from `EntityPool.For(prefab)`, one shared
pool per prefab: `Acquire(position, rotation)`, `Release(entity)`, `Release(entity, delay)`
(game time; a later Release replaces it). A script on the instance's root that implements
`IPoolable` gets `OnAcquire()` after activation and `OnRelease()` before deactivation -
reset state there instead of in `OnStart`, which runs only once per instance.

## Sounds, lights, materials, particles

- Sounds: `Audio.PlayOneShot(clip, volume, pitch, bus)` (or with a world position) plays
  without an entity and returns an `AudioVoice` (`Stop`, `SetVolume`, `SetPitch`,
  `FadeTo`). `Audio.SetClipLimit(clip, maxVoices, minInterval)` stops coin-spam stacking;
  `Audio.FadeBusVolume(bus, target, seconds)` and `AudioSourceComponent.FadeTo(volume,
  seconds, stopAtEnd)` ramp on unscaled time.
- Lights: `LightComponent.enabled = false` is off (no light, shadows or GI); intensity 0
  still renders shadow maps.
- Materials per renderer slot: `model.SetFloat(MaterialProperty.EmissiveIntensity, 3f, slot)`,
  `SetColor(MaterialProperty.EmissiveColor, c, slot)`, `SetVector(MaterialProperty.Tiling, v)`,
  `SetTexture(MaterialProperty.ColorMap, tex)`, `SetCullMode`, `SetAlphaMode`, the `Get*`
  pairs and `ResetMaterial(slot)`. The first write gives that renderer its own copy of the
  shared material; other renderers keep the asset.
- Particles: `emitter.Emit(count)` or `Emit(count, new EmitParams { position = p, velocity = v,
  color = c, size = s, lifetime = t })` bursts regardless of rate or play state.
  Gradients are `Gradient` (color keys), `MinMaxCurve` (random between two) and `Curve`
  (`colorGradient`, `scaleGradient`, `velocityGradient`, ...). Setting `emissionRate` keeps
  the live particles; `simulationSpace`, `lifetime` and `startDelay` clear them.

## Random is the engine's

The math types (`Vector3`, `Mathf`, `Random`, `Color`, ...) are in the global namespace, so
`Random` is the engine's `Random` (`Random.Range`, `Random.Float()`). With `using System;`
it hides `System.Random`: write `System.Random` in full, or alias it with
`using SystemRandom = System.Random;`.

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
