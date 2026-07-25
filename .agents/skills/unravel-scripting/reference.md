# Scripting Reference

## Native component C# parity

Expose via: C# wrapper in `engine_data/data/scripts/scene/components/` +
`native_comp_lut::register_native_component` in `scene_bindings.cpp` +
`*_bindings.cpp` + declare/register in `script_bindings.h` / `script_glue.cpp`.

| Native | C# | Notes |
|--------|----|-------|
| `transform_component` | `TransformComponent` | Full |
| `id_component` | `IdComponent` | Marker |
| `model_component` | `ModelComponent` | enabled, mesh, shadows, static, LOD bias, materials |
| `camera_component` | `CameraComponent` | FOV/clip/projection/masks + screen queries |
| `light_component` | `LightComponent` | type, color, intensity, range, spot, shadows |
| `skylight_component` | `SkylightComponent` | mode, turbidity, clouds, irradiance, cubemap |
| `reflection_probe_component` | `ReflectionProbeComponent` | type/method/update/resolution + `MarkDirty` |
| `physics_component` | `PhysicsComponent` | Strong |
| `character_controller_component` | `CharacterControllerComponent` | Strong |
| `animation_component` | `AnimationComponent` | play/blend/speed/autoplay/root motion/clip |
| `audio_source_component` | `AudioSourceComponent` | Strong |
| `audio_listener_component` | `AudioListenerComponent` | Marker |
| `bone_component` | `BoneComponent` | `boneIndex` |
| `submesh_component` | `SubmeshComponent` | entry enable/shadow/material override |
| `text_component` | `TextComponent` | Strong |
| `particle_emitter_component` | `ParticleEmitterComponent` | Strong |
| `ui_document_component` | `UIDocumentComponent` | Strong |
| `volume_component` | `VolumeComponent` | mode/priority/weight/blend/extents |

**Intentional non-exposures (do not add as `Component` types):**

- `tag_component` / `layer_component` — use `Entity.name` / `Entity.tag` / `Entity.layers`
- `prefab_component` / `prefab_id_component` — use `Prefab` asset + `Scene.Instantiate`
- `test_component` — editor/dev serialization smoke test only
- `root_component` / `active_component` — runtime markers; active is on `Entity`
- `script_component` — host for user `ScriptComponent` subclasses, not Add-able as native type

## script_system init hooks

From `script_system.cpp` event priorities:

| Event | Priority | Handler |
|-------|----------|---------|
| `on_play_begin` | -1000 | early setup |
| `on_play_end` | 1000 | teardown |
| `on_pause` | 100 | pause scripts |
| `on_resume` | -100 | resume scripts |
| `on_frame_late_update` | -100000 | late script update |

Use explicit priorities when adding new script event handlers.

## ScriptComponent fields

Typically stores:

- Script class name / assembly reference
- Instance handle (`dotnet::object` pinned via `dotnet::make_object_pinned`)
- Enabled state

Meta registration required for inspector editing.

## Recompile triggers

Editor menu: Header -> Recompile -> Scripts (Engine/Editor/Project)

Event: `on_script_recompile(ctx, protocol, version)`

## Debug config

`script_system::set_debug_config(address, port, loglevel)` — managed debugger attachment via `dotnet::debugging_config`.

## Adding C# API method

1. Implement native function in glue (`script_glue.cpp` / `script_interop.cpp`)
2. Register with `dotnet::internal_call_registry` and `dotnet_internal_call()`:

```cpp
auto reg = dotnet::internal_call_registry("Unravel.Core.MyClass");
reg.add_internal_call("internal_m2n_do_thing", dotnet_internal_call(internal_m2n_do_thing));
```

3. Add public C# wrapper using `[MethodImpl(MethodImplOptions.InternalCall)]`
4. **XML-document** the public type/member (`/// <summary>`, `<param>`, `<returns>` as applicable)
5. Test hot-reload and play mode

## POD type registration

For layout-compatible value types shared between C++ and C#:

```cpp
// script_interop.h — engine POD structs in managed_interface
dotnet_register_converter_for_pod(math::vec3, dotnetpp_backend::managed_interface::vector3);
```

Converter specializations live in `dotnetpp_backend::managed_interface` (`.cpp`):

```cpp
namespace dotnetpp_backend {
namespace managed_interface {
template<>
auto converter::convert(const math::vec3& v) -> vector3
{
    return {v.x, v.y, v.z};
}
}}
```

`dotnetpp_backend` is a macro (defined in `dotnet_managed.h`) that expands to the active
backend namespace. Prefer calling through `dotnet::managed_interface::converter` for
conversions in glue code. Use `dotnet::` for everything else (`object`, `domain`,
`type`, `get_managed_ptr`, etc.).

Custom reference-type converters specialize `dotnet_converter<T>` with `dotnet::managed_ptr`
as the managed handle type.

## Collision / sensor callbacks

Physics system forwards to script_system -> C#:

- `on_collision_enter` / `exit`
- `on_sensor_enter` / `exit`

Bridge code in physics + script glue — update both sides. Manifold points use
`dotnetpp_backend::managed_interface::manifold_point`.

## dotnetpp API (current)

| Type / helper | Name |
|---------------|------|
| Domain | `dotnet::domain` |
| Assembly | `dotnet::assembly` |
| Type | `dotnet::type` |
| Object | `dotnet::object` |
| Method | `dotnet::method` |
| Field | `dotnet::field` |
| Property | `dotnet::property` |
| Array | `dotnet::array<T>` |
| List | `dotnet::list<T>` |
| Exception | `dotnet::exception` |
| Internal call | `dotnet_internal_call(f)` |
| Converter | `dotnet_converter<T>` |
| POD register | `dotnet_register_converter_for_pod` |
| Converter specialize | `dotnetpp_backend::managed_interface::converter` |

Include `<dotnetpp/dotnetpp.h>` (umbrella) or individual `dotnetpp/dotnet_*.h` headers.
Link `dotnetpp` in CMake.

## Domain unload, statics cleanup, leak detection

CoreCLR domains are collectible `AssemblyLoadContext`s — a static field in a *surviving*
assembly (engine managers, caches keyed by script `Type`s, event subscriptions) silently
pins the unloaded domain forever.

`Bridge.Unload.cs` (clrpp managed side) handles this on every domain unload:

1. **`[AutoStaticsCleanup]`** (defined in `Unravel.Core`, matched by attribute *name* so any assembly can define its own copy): all marked types in all clrpp contexts get cleaned. If the type defines `static void OnStaticsCleanup()` it is invoked (use to re-create managers — e.g. `SystemManager`, `UIEventManager`); otherwise all non-readonly static fields are reset to defaults (readonly fields warn — use `OnStaticsCleanup`).
2. Interned reflection handles owned by the dying context are purged.
3. Unload is **verified** via `WeakReference` after GC; `bridge().domain_unload()` returns 0 = clean, 1 = leaked, -1 = error.
4. On leak, a diagnostic scan of the surviving contexts' static fields (values, delegate targets, shallow collection contents incl. `Dictionary<Type,...>` keys) logs every root, e.g. `static root: Foo.Bar.cache contains key MyScript`.
5. Also on leak, `Bridge.LeakAnalysis.cs` asks the GC itself via ClrMD (`Microsoft.Diagnostics.Runtime`, optional dll next to the bridge): snapshots the process (`DataTarget.CreateSnapshotAndAttach`) and reports (a) strong/pinned GC handles targeting leaked-domain objects — i.e. handles native code did not release, (b) a census of surviving instances per type, (c) full root paths (`Stack/handle root -> Holder -> ... -> LeakedObject`). Zero steady-state overhead — the snapshot only happens after a leak was already detected.

Classes with statics that hold script instances/Types/delegates MUST be marked `[AutoStaticsCleanup]`.

Interned-handle purge gotcha: ownership checks members by **DeclaringType and ReflectedType** — a `PropertyInfo` declared on an engine base class but reflected through an app type pins the app domain via `m_reflectedTypeCache`.

### Managed bridge deployment

The bridge payload (`Clrpp.Managed.dll` + `Clrpp.Managed.runtimeconfig.json` + optional NuGet deps: `Mono.Cecil*` for icall weaving, `Microsoft.Diagnostics.*`/`Microsoft.Extensions.*` for leak analysis) is deployed by CMake into a **`clrpp/` subfolder** next to the executables. The native loader (`clr_bridge.cpp initialize`) probes: explicit `compiler_paths::assembly_dir` (and its `clrpp/` child), `<exe_dir>/clrpp`, `<exe_dir>`, `<cwd>/clrpp`, `<cwd>`. The bridge resolves its own dependencies from its own directory (`Resolving` hook in `Bridge.Core.cs`), so the whole folder is self-contained — ship the `clrpp/` folder as-is when deploying. Optional dlls may be omitted from a shipped game: without Cecil, icall weaving is disabled (scripts with `[InternalCall]` will not work on CoreCLR until woven); without ClrMD, leak reports lose the GC-snapshot analysis.
