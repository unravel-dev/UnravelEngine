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
- Instance handle (Mono object)
- Enabled state

Meta registration required for inspector editing.

## Recompile triggers

Editor menu: Header → Recompile → Scripts (Engine/Editor/Project)

Event: `on_script_recompile(ctx, protocol, version)`

## Debug config

`script_system::set_debug_config(address, port, loglevel)` — Mono debugger attachment.

## Adding C# API method

1. Implement native function in glue
2. Export to Mono via `monort` bindings
3. Add static wrapper in C# utility class
4. Document in script template if user-facing
5. Test hot-reload and play mode

## Collision / sensor callbacks

Physics system forwards to script_system → C#:

- `on_collision_enter` / `exit`
- `on_sensor_enter` / `exit`

Bridge code in physics + script glue — update both sides.
