# Expose an engine feature to C#

Bindings live in `engine/engine/scripting/ecs/systems/bindings/<area>_bindings.cpp`, one
`register_<area>_script_bindings()` per area, declared in `bindings/script_bindings.h` and called from
`script_system::bind_internal_calls` (`engine/engine/scripting/ecs/systems/script_glue.cpp`). Names:
`internal_m2n_*` for C# calling C++, `internal_n2m_*` for C++ calling C#.

## A method

1. The native function, in the area's binding file, taking plain values: an entity as
   `entt::entity` (resolved per call with `safe_get_component<T>(id)` or `get_entity_from_id`), an
   asset as its uid, strings and PODs by value.
2. Register it under the managed type's full name:

   ```cpp
   auto reg = dotnet::internal_call_registry("Unravel.Core.VolumeComponent");
   reg.add_internal_call("internal_m2n_volume_get_mode", script_internal_call(internal_m2n_volume_get_mode));
   ```

   `script_internal_call` for anything that reads or writes engine state; `dotnet_internal_call` only
   for calls safe on any thread.
3. The C# side, in the matching file under `engine_data/data/scripts/`:

   ```csharp
   [MethodImpl(MethodImplOptions.InternalCall)]
   private static extern byte internal_m2n_volume_get_mode(Entity eid);
   ```

   with a public wrapper that carries the XML docs (`summary`, `param`, `returns`). The icall weaver
   rewrites these declarations when the script library builds.
4. A new area: declare `register_<area>_script_bindings()` in `bindings/script_bindings.h` and call it
   from `bind_internal_calls`.

## A native component

- C# wrapper `engine_data/data/scripts/scene/components/<Name>Component.cs` deriving `Component`, its
  properties calling `internal_m2n_<name>_*` with `owner`.
- `native_comp_lut::register_native_component<<name>_component>("Unravel.Core.<Name>Component")` in
  `bindings/scene_bindings.cpp`, so `Entity.GetComponent` / `AddComponent` know it.
- A `<name>_component_bindings.cpp` with its register function, as above.
- The component's other parity items (meta, serialization, inspector, MCP): `unravel-ecs-component`.
- Components deliberately not exposed are listed in [SKILL.md](SKILL.md) "How it works".

## A value type passed by value

- A layout-compatible struct in `dotnetpp_backend::managed_interface` (`script_interop.h`), the two
  `converter::convert` specializations in `script_interop.cpp`, and
  `dotnet_register_converter_for_pod(native_type, managed_struct)` in `script_interop.h`.
- The C# struct declares `[StructLayout(LayoutKind.Sequential)]` and the same fields in the same
  order (`engine_data/data/scripts/math/Vector3.cs`). A blittable payload struct (`script_system::frame_update_data`,
  `ui_event_payload`) is changed on both sides in one change.
- A reference-type conversion specializes `dotnet_converter<T>` with `dotnet::managed_ptr` as the
  managed type (see the `hpp::small_vector` one in `script_interop.h`).

## Calling into C#

- Cache the `dotnet::method` / `dotnet::method_invoker` once per engine domain load in
  `script_system::engine_script_cache` (`script_system.h`); building an invoker per frame costs bridge
  round trips for its signature check alone.
- Call only from the script thread. Work recorded on pool threads (animation events, physics contacts)
  is delivered from the main thread, as `dispatch_animation_events` does.
- Physics callbacks reach scripts through `script_system::on_collision_enter` / `on_collision_exit` /
  `on_sensor_enter` / `on_sensor_exit` / `on_joint_break`; the physics side and the glue change together.

## A new type for script members

A type a script's public member may hold (an asset, a math type) needs, besides its C# wrapper:

- a save and a load entry in the serializer maps of
  `engine/engine/meta/ecs/components/script_component.cpp`;
- a widget in `editor/editor/hub/panels/inspector_panel/inspectors/inspector_script.cpp`;
- a conversion in `editor/editor/system/mcp/mcp_component_utils.cpp` for the MCP property tools.

## Verify

- Build `engine` and `editor`, then in an editor write a script that calls the new API, wait for the
  build, enter play and check the result. Run the call from a `Task.Run` body once: a
  `script_internal_call` binding must throw `InvalidOperationException` there.
- `scripts_api_search` finds the new member with its summary.
