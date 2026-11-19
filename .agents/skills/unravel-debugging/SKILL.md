---
name: unravel-debugging
description: >-
  Investigates UnravelEngine bugs, crashes, hangs and slowdowns: reproducing on the user's
  scene and settings, logs and crash dumps, re-entrant waits, the profiler (CPU scopes, GPU
  passes, stats overlay, GPU eviction), player GPU logs, and A/B measurement discipline. Use
  for a bug report, crash, regression, frame drop, memory pressure, "does not reproduce", or
  before claiming a fix or a speed-up.
---

# Debugging and measuring

Finding the cause of a bug, crash or slowdown and proving the fix or the speed-up. Owns the
profiler (`engine/engine/profiler/`) and its panel (`editor/editor/hub/panels/profiler_panel/`).
Measurement discipline and timing setups: [measuring.md](measuring.md). Each area's own traps
stay in its skill (GI and shader measuring: `unravel-rendering`); starting and driving an
editor: `unravel-editor-mcp`.

## Gotchas

Reproducing

- Reproduce under the conditions the user named - their object, their scene state (a
  screenshot is a state), their settings read back - and replay the engine's own code path
  against that state before choosing a cause. An effect found under other conditions is a
  different bug.
- Change the user's scene with reversible operations only. A reference frame comes from
  moving the object far away and back, never from deactivating or deleting it: a full atlas
  or pool can refuse it on its return.
- Before reading an instrument's silence as "never happens", make it report something: the
  profiler records nothing until recording starts, and a debug view or a log needs its switch.
- List an engine bug only after an isolated repro (one test entity or emitter, its state
  queried, a before and after capture); otherwise report it as unverified.
- A fix adds no persistent state (settings, caches, remembered values) the user did not ask
  for; ask first when it seems to need some.

Crashes, hangs and logs

- An intermittent crash in code that is correct in isolation has a trigger. A blocking
  threadpp call (`wait`, `wait_for`, `sleep_for`, a future's wait, a blocking asset load) runs
  the queued tasks of its thread where it blocks; only on-process tasks (`invoke_on_process`,
  `async_on_process`) wait for the thread's own `process()`
  (`deps/threadpp/threadpp/threadpp/thread.h`). List what can run in between (MCP tool jobs
  posted to the main thread, asset watcher re-syncs, events), trace it with stack-printing
  breakpoints on the destructive entry points (cdb), then build a stress that drives that
  interleaving. Do not loop restarts. Work that frees state others hold across a wait is queued
  with `invoke_on_process`.
- `<bin>/CrashLog.txt` is append-only, older crashes above; on Windows `CrashLog.dmp` beside
  it is the last crash's minidump.
- The editor and the runner write `<bin>/Log.txt`, rewritten by the next program started from
  that folder; a player writes `user:/logs/Log.txt`, or beside its binary with `--log-here`
  (always on Android). The file is flushed only on errors: reading logs and waiting on an
  editor are in `unravel-editor-mcp` ("Logs and waiting").
- Count what should exist (compiled files per source, entities per scene) rather than trusting
  a silent log.

Changing code

- A call that looks duplicate can be on purpose: read its history (`git log -L`, blame) and
  every sibling call of the API, and measure the behaviour before and after removing it.
  `seq::update(delta_t::zero())` after `on_frame_update` in `engine/engine/engine.cpp` starts
  the actions queued that frame and advances nothing.
- When a standard unit or API does not do what its definition says, check what the engine
  feeds it before writing a workaround, and before turning a workaround into guidance (RmlUi's
  `dp` was fed the viewport size instead of the display scale).
- A comment that says why a better option is left out (encode time, memory, a driver bug)
  records a decision. Bring it as a question with its cost measured, never as a clear win; a
  documented guard needs its reason disproved before it goes.
- A check that passes must be able to fail. Before citing one, know what it would show if
  the thing were broken: binaries confirmed rebuilt, a pattern that cannot match something
  else, code the test actually reaches.

Performance

- Read the stats before optimizing: a CPU-bound and a GPU-bound frame need different fixes.
  The stats overlay and MCP `stats_get` count draw calls without the editor UI.
- Measure GPU costs with vsync off (`--novsync`, or MCP `editor_set_frame_pacing` with
  `vsync: false`): under vsync the GPU idles between frames, pass times read about twice as
  high, swing between launches and do not add up. Judge visual quality with vsync on.
- Deferring slow work to first use (program links, distance-field bakes) moves the stall into
  play. Make the work cheaper or move it to loading, the asset compile or the deploy, and say
  which frame pays.
- Measure the premise first, take a baseline in the same session, and prove the changed path
  ran: [measuring.md](measuring.md).

## How it works

- Profiler (`engine/engine/profiler/profiler.h`): each thread records `APP_SCOPE_PERF` scopes
  into its own ring (65536 events) with wall and thread CPU time, and the main thread takes the
  finished events once a frame; neither side waits. A thread inside a scope when events are
  taken (the render thread runs out of step with the frame) hands them over once the scope
  closes; new events are dropped while the main thread still has a full ring to take. While
  the profiler is stopped a process-wide gate skips recording. `performance_profiler` keeps
  frame snapshots while recording (stopped, recording, paused).
- MCP: `profiler_get_cpu_scopes` (wall ms per scope name, averaged over frames, prefix filter)
  starts a stopped profiler, so its first call returns no frames; `profiler_get_gpu_passes`
  reads bgfx's per-view GPU times; `stats_get` returns what the overlay shows, bgfx texture and
  render-target memory included.
- Players: `--gfx-profile` logs every render pass's GPU time every few seconds,
  `--gfx-frame-log` the frame and GPU frame times without per-pass timers, `--gfx-stats` draws
  bgfx's statistics over the frame (`--gfx-stats-gesture`: a four-finger tap toggles them), and
  `--gfx-disable <features>` renders as a device without those features would
  (`engine/engine/rendering/renderer.cpp`).
- The stats overlay (`editor/editor/hub/panels/viewport_stats_overlay.cpp`) shows fps, CPU and
  GPU ms, draw counts, GPU memory and eviction; the profiler panel adds the timeline, GPU frame
  stats, submit latency, GPU resources and eviction sections.
- GPU eviction (`engine/core/graphics/eviction.h`, `gfx::eviction::get_stats()`) stays off when
  the backend reports no memory budget, and on D3D11 and OpenGL. `thrash_events` counts drops a
  read undid within `min_age_frames` (default 60). Such a resource then stays unread that much
  longer before it drops again (up to 16 minimum ages), so reads on a cycle stop churning; a count
  that keeps rising means reads come back after longer gaps, or the budget is too tight. Textures
  keep their mip tails when evicted (`reduced_count`, `saved_bytes`); `raise_backlog` counts
  textures drawn lately that still wait to sharpen: a tight budget, or a low `max_upload_mb`. The
  test suites cannot see a budget: suite "texture residency" drives a registry of its own.
- Check paging under real pressure, not only under a manual budget: a manual budget counts the
  engine's own bytes exactly, while the automatic one judges bgfx's memory stats, which lag a few
  frames, move with the OS budget, and on Vulkan keep up to 128 MiB of freed allocations for reuse.
  In an editor on Vulkan or D3D12, MCP `gpu_eviction_test` `simulate_gpu` (or the profiler's
  simulated GPU sizes) reserves real GPU memory. The drop goes from the soft budget down to the
  target (85% and 75% of the GPU by default, 2.3 GB on a 24 GB card): a scene with less to evict
  never reaches it, so narrow `graphics.eviction.target_fraction` to also test a target it reaches.
- Windows drivers page GPU memory out when it runs short; the Linux and Android drivers do not, so there a
  Vulkan allocation past free VRAM fails, and bgfx outside its debug config carries on with the half-made
  texture until the device is lost (OpenGL on the same machine pages and works). A reserve on Windows
  never shows this. The device budget there keeps 256 MiB plus 4% free (`get_safety_margin`), and a
  draw's create of a texture with a tail takes the largest level that fits: `short_reads` in the
  paging stats counts the draws that got fewer mips.
- Crash handlers (`engine/core/crash/crash.hpp`): SEH and CRT signals on Windows, `sigaction`
  elsewhere, and `std::terminate`; a crash-safe write to the crash log comes first, then the
  minidump.

## Procedures

A bug or a crash:

1. Collect the user's conditions: scene, object, edit or play mode, settings, platform,
   `Log.txt`, `CrashLog.txt`.
2. Reproduce there on an editor you started (`unravel-editor-mcp`), then reduce to an
   isolated repro.
3. A crash: read the stack in `CrashLog.txt` or the dump, and list what else runs on that
   thread between the frames involved.
4. When practical, add the failing check to the area's suite first (`unravel-build-verify`,
   test-suites.md), then fix the root cause.
5. Verify under the user's conditions again; enter and leave play mode, and save and reload
   when data is involved.

A slowdown:

1. `stats_get` or the overlay: CPU or GPU bound, draw counts, memory.
2. CPU: `profiler_get_cpu_scopes` with a prefix; add `APP_SCOPE_PERF` scopes where the time
   is unaccounted. GPU: `profiler_get_gpu_passes` with vsync off.
3. Measure the attribution before building the fix, then A/B as [measuring.md](measuring.md)
   says.

The report: the root cause in one sentence, the fix, what was verified and what was not.

## Verify

- Profiler code: `unravel-tests --suite "profiler threads"` and `--suite "thread cpu"`; build
  `editor` for the panel.
- A fix: the user's repro passes, the area's suites pass, and the check that pins the fix
  fails without it.
- A speed-up: an interleaved same-session A/B shows it ([measuring.md](measuring.md)).

## Entry points

| File | What it holds |
|------|---------------|
| `engine/engine/profiler/profiler.h` | rings, capture gate, scope macros, frame snapshots |
| `editor/editor/hub/panels/profiler_panel/profiler_timeline_panel.cpp` | the profiler panel |
| `editor/editor/hub/panels/viewport_stats_overlay.cpp` | the stats overlay |
| `editor/editor/system/mcp/mcp_tools_viewport.cpp` | profiler MCP tools |
| `engine/core/graphics/eviction.h` | GPU eviction budget and stats |
| `engine/core/crash/crash.hpp` | crash handlers, crash log, minidump |
| `engine/core/logging/logging.cpp` | log sinks, flush on errors |
| `engine/engine/engine.cpp` | log files, frame loop order |
| `deps/threadpp/threadpp/threadpp/thread.h` | what blocking calls run |
| `engine/engine/rendering/renderer.cpp` | graphics command-line options |
