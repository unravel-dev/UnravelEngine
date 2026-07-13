# Scripting Reference

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

Editor menu: Header → Recompile → Scripts (Engine/Editor/Project)

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

3. Add static wrapper in C# utility class using `[MethodImpl(MethodImplOptions.InternalCall)]`
4. Document in script template if user-facing
5. Test hot-reload and play mode

## POD type registration

For layout-compatible value types shared between C++ and C#:

```cpp
// script_interop.h — engine POD structs in mono::managed_interface
dotnet_register_converter_for_pod(math::vec3, mono::managed_interface::vector3);
```

Converter specializations live in `dotnetpp_backend::managed_interface` (`.cpp`):

```cpp
namespace dotnetpp_backend {
namespace managed_interface {
template<>
auto converter::convert(const math::vec3& v) -> mono::managed_interface::vector3
{
    return {v.x, v.y, v.z};
}
}}
```

`dotnetpp_backend` is a macro (defined in `dotnet_managed.h`) that expands to `mono` or `clr`. Converter specializations use:

```cpp
namespace dotnetpp_backend::managed_interface {
template<>
auto converter::convert(const math::vec3& v) -> mono::managed_interface::vector3 { ... }
}
```

The preprocessor expands `dotnetpp_backend` to the active backend — no `#ifdef` in user code. Use `dotnet::` for everything else (`object`, `domain`, `get_managed_ptr`, etc.).

Custom reference-type converters specialize `dotnet_converter<T>` with `dotnet::managed_ptr` as the managed handle type.

## Collision / sensor callbacks

Physics system forwards to script_system → C#:

- `on_collision_enter` / `exit`
- `on_sensor_enter` / `exit`

Bridge code in physics + script glue — update both sides. Manifold points use `mono::managed_interface::manifold_point`.

## dotnetpp API quick map

| Old (`mono::`) | New (`dotnet::`) |
|----------------|------------------|
| `mono_domain` | `domain` |
| `mono_assembly` | `assembly` |
| `mono_type` | `type` |
| `mono_object` | `object` |
| `mono_method` | `method` |
| `mono_field` | `field` |
| `mono_property` | `property` |
| `mono_array<T>` | `array<T>` |
| `mono_list<T>` | `list<T>` |
| `mono_exception` | `exception` |
| `internal_call(f)` | `dotnet_internal_call(f)` |
| `mono_converter<T>` | `dotnet_converter<T>` |
| `register_basic_mono_converter_for_pod` | `dotnet_register_converter_for_pod` |
| `mono::managed_interface::converter` (specialize) | `dotnetpp_backend::managed_interface::converter` |

Include `<dotnetpp/dotnetpp.h>` (umbrella) or individual `dotnetpp/dotnet_*.h` headers. Link `dotnetpp` in CMake (not `monopp` directly).

## Domain unload, statics cleanup, leak detection (CoreCLR)

Mono destroys a domain wholesale (statics + instances). CoreCLR domains are collectible `AssemblyLoadContext`s — a static field in a *surviving* assembly (engine managers, caches keyed by script `Type`s, event subscriptions) silently pins the unloaded domain forever.

`Bridge.Unload.cs` (clrpp managed side) handles this on every domain unload:

1. **`[AutoStaticsCleanup]`** (defined in `Unravel.Core`, matched by attribute *name* so any assembly can define its own copy): all marked types in all clrpp contexts get cleaned. If the type defines `static void OnStaticsCleanup()` it is invoked (use to re-create managers — e.g. `SystemManager`, `UIEventManager`); otherwise all non-readonly static fields are reset to defaults (readonly fields warn — use `OnStaticsCleanup`).
2. Interned reflection handles owned by the dying context are purged.
3. Unload is **verified** via `WeakReference` after GC; `bridge().domain_unload()` returns 0 = clean, 1 = leaked, -1 = error.
4. On leak, a diagnostic scan of the surviving contexts' static fields (values, delegate targets, shallow collection contents incl. `Dictionary<Type,...>` keys) logs every root, e.g. `static root: Foo.Bar.cache contains key MyScript`.
5. Also on leak, `Bridge.LeakAnalysis.cs` asks the GC itself via ClrMD (`Microsoft.Diagnostics.Runtime`, optional dll next to the bridge): snapshots the process (`DataTarget.CreateSnapshotAndAttach`) and reports (a) strong/pinned GC handles targeting leaked-domain objects — i.e. handles native code did not release, (b) a census of surviving instances per type, (c) full root paths (`Stack/handle root -> Holder -> ... -> LeakedObject`). Zero steady-state overhead — the snapshot only happens after a leak was already detected.

Classes with statics that hold script instances/Types/delegates MUST be marked `[AutoStaticsCleanup]`.

Interned-handle purge gotcha: ownership checks members by **DeclaringType and ReflectedType** — a `PropertyInfo` declared on an engine base class but reflected through an app type pins the app domain via `m_reflectedTypeCache`.

### Managed bridge deployment (CoreCLR)

The bridge payload (`Clrpp.Managed.dll` + `Clrpp.Managed.runtimeconfig.json` + optional NuGet deps: `Mono.Cecil*` for icall weaving, `Microsoft.Diagnostics.*`/`Microsoft.Extensions.*` for leak analysis) is deployed by CMake into a **`clrpp/` subfolder** next to the executables. The native loader (`clr_bridge.cpp initialize`) probes: explicit `compiler_paths::assembly_dir` (and its `clrpp/` child), `<exe_dir>/clrpp`, `<exe_dir>`, `<cwd>/clrpp`, `<cwd>`. The bridge resolves its own dependencies from its own directory (`Resolving` hook in `Bridge.Core.cs`), so the whole folder is self-contained — ship the `clrpp/` folder as-is when deploying. Optional dlls may be omitted from a shipped game: without Cecil, icall weaving is disabled (scripts must use `InternalCalls.Bind`); without ClrMD, leak reports lose the GC-snapshot analysis.
