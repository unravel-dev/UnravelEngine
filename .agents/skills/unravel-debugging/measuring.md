# Measuring and A/B

How to get numbers that hold in this engine: premises, baselines, measurement scripts, what
to measure, and the timing setups. GI and shader measuring add their own traps
(`unravel-rendering`).

## Contents

- Before building
- Baselines and A/B
- Measurement scripts
- Measure what the user sees
- CPU and GPU timing
- Memory
- On a phone
- A/B against the last commit

## Before building

- Search the local plans and measurements (the git-ignored tasks folder) for a measured
  verdict on the same shape - grep the kernel, function or pass name - before building it
  again.
- Measure the premise of an optimisation first. Force the suspected path on and off (a flag,
  an emulation variant) and read the cost under the scenario the change targets. A change that
  cannot show a win in its own scenario is reverted, however sound its derivation.
- An emulation feeds the design its own inputs. Before it borrows another consumer's data (a
  dirty list, a region set), read what that consumer inflates, clamps or ages the data for,
  and build the design's input when they differ. A neutral verdict on the wrong input closes
  nothing.
- A neutral verdict on a mechanism with a measured ceiling gets two diagnostics before any
  redesign - the mechanism forced on everywhere (its ceiling) and compiled in but never firing
  (its code cost) - plus a count of how often it fires.

## Baselines and A/B

- A baseline comes from the same session. A number from an earlier build, command, hour or
  session is not one: background load and thermal state move short benchmarks by 20% and more.
- Compare two builds interleaved: keep both binaries and alternate them in one loop, for
  example `cp unravel-tests.exe tests-a.exe`, rebuild, then run a, b, a, b. Device runs use
  ABBA order.
- Editors of two build trees each keep the window layout of their own bin (`<bin>/imgui.ini`),
  so their Game views can render a different number of pixels. Before an editor A/B, copy one
  tree's `imgui.ini` into the other (keep the original to put back) and record the view size
  with every reading (`viewport_capture_game` returns it).
- When a result resists explanation after two or three targeted experiments, rebuild the
  baseline and measure it again. If it moved, every comparison against it is void.
- Fixtures that take tens of milliseconds are dominated by pool dispatch and drift most;
  weight the seconds-scale ones.
- Prove the changed code ran during the measurement (a log line or a counter at the branch).
  When it did not, the run was an A/A and measured the metric's floor.
- Take the noise floor from an A/A run of identical code, not from two samples. A "first frame
  after X" metric needs a frame-locked capture.
- A new experiment switch gets one sanity run that shows it changes the output before its
  results are read.
- A shader variant A/B: confirm each side runs its own compiled binary (hash the compiled
  file; a sanity variant whose effect cannot be missed proves the program is live). The shader
  edit loop is in `unravel-rendering` (shader-change.md).

## Measurement scripts

- Read a scene value before nudging it, print it, and restore the value read, never a literal.
  When a state "collapses" after an edit, check the edit's actual before and after first.
- Build every configuration from a saved baseline (the settings captured right after a fresh
  load) and restore it in a `finally` block; never build on top of the live state a previous
  script left. A result far off the last run is checked against the editor's live settings
  before it is reported.
- Do not touch the editor (flags, views, panels) while a script measures.
- Metrics that settle (auto exposure, temporal accumulation) are read at a fixed time since
  launch or since the change, the same for every variant. Measure a settle curve in one
  continuous run.
- Run on an editor you started, on your own MCP port (`unravel-editor-mcp`).

## Measure what the user sees

- Before the first fix, capture the user's own view at the user's settings (read them back),
  and capture it again after every landing; report that number first. Component metrics decide
  what to land; they do not say the work is done. When the headline does not move after two
  landings, re-derive where the cost or the light goes before landing a third.
- Build the metric on the surface the user names - a normals debug view separates floor from
  walls in one capture - and confirm the mask with one look. An average over a large region
  hides the few hundred pixels the user circled.
- For a visual change, send before and after screenshots of every attempt: the same binary,
  the same pose, a frame-exact capture (MCP `viewport_measure_temporal` marks the frame motion
  stops on), vsync on, the user's settings.
- Frozen or nearest-sample debug views are mechanism probes, not the score: they exaggerate
  small tracking errors.
- When a stored value never changes under forced updates, first prove the store happens:
  stamp a marker at the entry (visited), then inside the suspected block (entered), then make a
  neutral edit of the block (sensitive to code generation). Only then look for what carries
  the value.

## CPU and GPU timing

- CPU scopes: `APP_SCOPE_PERF("Name")` around the code, read with MCP
  `profiler_get_cpu_scopes` (averaged over frames, filtered by prefix) or the profiler panel.
- A whole process on Windows: VTune's command line samples in software mode only on AMD
  processors - `vtune -collect hotspots -knob sampling-mode=sw -target-pid <pid> -duration 12 -r <dir>`,
  then `vtune -report top-down -r <dir> -format csv -csv-delimiter tab`. Threads blocked in
  waits are not sampled, so lock sleeps never show. RelWithDebInfo symbols resolve.
- GPU in the editor: vsync off (`--novsync` or MCP `editor_set_frame_pacing`), then MCP
  `profiler_get_gpu_passes`. An in-session switch beats a relaunch A/B: costs swing between
  launches.
- GPU in a player: `--gfx-profile` for per-pass times, `--gfx-frame-log` for frame and GPU
  frame times.

## Memory

- GPU memory bgfx tracks: MCP `stats_get` (texture and render-target memory).
- What the driver commits for a process (Windows):
  `Get-Counter '\GPU Process Memory(pid_<PID>_*)\Dedicated Usage'` summed over its instances,
  median of 5 samples, about 10 s after a toggle. Toggle the feature within one session and
  take on minus off: absolute values drift by tens of MiB between sessions. Drivers do not
  commit render targets that are never written, so allocated is not resident.

## On a phone

- Frame and pass times of the same build swing 15-30% between runs (GPU clock modes). Run ABBA
  in one session. The verdict is the raw GPU frame time (`--gfx-frame-log`); per-pass ratios
  against passes the change cannot touch (`--gfx-profile`) are a hint, and fail when the
  change alters bandwidth.
- Heat: force-stop the game after each run, wait for Thermal Status 0 and a cool skin
  (`adb shell dumpsys thermalservice`, read-only), then start the next run; repeat the
  reference stage at the end as a control. Never change phone settings to cool it.
- On Samsung Mali phones the GPU clock is capped by heat without a change of Thermal Status:
  sample `/sys/kernel/gpu/gpu_clock` and `gpu_max_clock` during every measurement
  (`MSYS_NO_PATHCONV=1 adb shell cat ...` from Git Bash) and compare settled windows only.
- `adb install -r --fastdeploy` reinstalls in seconds over wireless adb; skip reinstalling an
  unchanged package.

## A/B against the last commit

Only with the user's go-ahead: it adds a git worktree and a second build tree.

1. `git worktree add --detach <path> HEAD` beside the repository, then junction each
   submodule path from the main tree (`cmd /c mklink /J <worktree path> <main path>`).
2. Configure with the main tree's compilers, linker and resource compiler (from its
   `CMakeCache.txt`), build `editor engine_data editor_data`, and copy the tools (`shaderc`,
   `texturec`, `geometryc`, `dxcompiler.dll`) and `imgui.ini` from the main bin; without
   `imgui.ini` the views have another size and framing.
3. Start it with `--mcp-port <own port>` on a copy of the project, so two editors never share
   one project cache. Never start a second editor from a bin where another one runs.
4. `engine_data` copies the committed compiled shaders: compare each side's compiled files
   before trusting a null result.
5. To remove it, delete the junctions first (`cmd /c rmdir <junction>`): a recursive delete
   follows them into the main tree's submodules.
