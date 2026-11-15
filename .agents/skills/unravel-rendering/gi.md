# GI reference

Global illumination: the mesh distance fields and the global distance field, the surface cache (cards), the
screen probe gather with its radiance cache, the GI reflections, the short-range AO and their temporal filters.
World state, the SDF bake and the CPU card scene live in `engine/engine/rendering/gi/`, the `gi_*` passes in
`engine/engine/rendering/pipeline/passes/`, the shaders in `engine_data/data/shaders/gi/`. The design ports UE's
software ray traced GI (global tracing, Epic and High tiers); "UE" below names the port's reference. User-visible
text says "GI" only.

## Contents

- [Gotchas](#gotchas)
- [How it works](#how-it-works)
- [Changing GI](#changing-gi)
- [Measuring GI changes](#measuring-gi-changes)
- [Entry points](#entry-points)

## Gotchas

Contracts:

- `gi_constants.h` and `gi_pass_constants.h` own every GI constant, each row with a unit and a why. Their mirrors
  `gi_constants.sh` / `gi_pass_constants.sh` must match row for row: suite "gi constants" parses both directions.
- Values the C++ hands to shaders as uniforms (`gi_pass::make_settings_uniform`: the temporal frames, the trace
  distance; the quality layouts) do not change when only a deployed `.sh` mirror is edited. Grep how the consumer
  reads a value before trusting a deployed-constant variant.
- Distances are metres. The gather writes rgb = E / pi in the view's pre-exposed space, alpha = served. Its
  history alpha is the 4-bit frame count (multiples of the maximum / 15) the temporal reads back next frame.
- Persistent stores (card atlases, radiance cache) hold lighting at the constant
  `GI_CACHED_LIGHTING_PRE_EXPOSURE`; per-frame targets and histories are in the view's pre-exposure. A
  view-dependent scale in a persistent store would need invalidating on every exposure step.
- `surface_cache_system` lives in the context and serves every camera: `update_world` runs once per frame for
  one scene, and `is_world_scene` keeps passes of another scene off its buffers. The global distance field
  belongs to the render view (`surface_cache_view`): one cascade shared by two cameras re-snaps every frame.
- The surface cache (`gi_surface_cache_pass`) is shared too: the frame's first GI camera updates and captures
  the cards; every camera keeps its own object grid and lights the card pages nearest to it.
- The indirect pass takes `GI_RESOLVE` over SSIL by its presence alone. A frame without a gather removes
  `GI_RESOLVE` and `GI_ROUGH_SPECULAR` (`run_gi_pass`), or a frozen image keeps overriding SSIL.
- A GI view owns its reflections and AO: SSR steps aside when the GI reflections run (one reflection signal, as
  UE), GTAO / ASSAO when the short-range AO runs. The RBUFFER / PBUFFER composition is the indirect occlusion
  contract in [SKILL.md](SKILL.md).
- Skinned submeshes and translucent materials place no distance field: characters get GI from screen traces only.
- A material emitting at least `GI_EMISSIVE_LIGHT_SOURCE_MIN_LUMINANCE` makes its placements emissive light
  sources: cards resident down to one texel and composed into every global SDF level however small. An emitter
  inside a non-emissive placement at most 10x its size (a bulb in its glass) loses the flag
  (`clear_enclosed_emissive_light_sources`). Before attributing what a mover changes, list the emissive
  children under it: a small bright child is a light source however small it is.
- Objects whose bounding sphere is at most max(`GI_GLOBAL_SDF_MIN_OBJECT_RADIUS`, 0.5 voxel) stay out of the
  global SDF (`SdfGiCascadeKeepsInstance`) unless they are emitters; `scene.detail` scales the radius.
- Open one-sided meshes bake signed by a backface vote (inside when more than 25% of 98 rays hit back faces;
  every mip votes with the finest level's reach). Two-sided materials and doubled sheets bake unsigned shells
  that the global clipmap composes as zero-thickness sheets (`SdfSheetDistance`, `sample_mesh_sdf_sheet`): a
  new reader of the clipmap assumes sheets have no thickness.
- `mesh_sdf::encode_range` is 4 voxels, so a field's band reaches 4 of its voxels: a finer field reports
  distances over a shorter reach, which degrades every reader of a distance (clipmap compose, sphere-trace steps,
  soft shadows). A finer voxel and a wider band are one storage-format change.
- A coarser mip of a padded field is not proportionally cheaper: the padding grows with the voxel, so the chain
  ends where the next level would not be materially smaller.
- Walls thinner than the global SDF voxel (19.8 cm at level 0) are where light leaks; a sealed room must stay
  black at every wall thickness the test scenes hold.
- Screen traces read Hi-Z texels at uv x textureSize(mip); `cs_hiz_downsample.eshader` takes 3 texels along an
  odd dimension to stay conservative. A screen trace that skips walls near the lower or right edge points at the
  Hi-Z mapping first.
- `camera::get_frustum()` is TAA-jittered: a cache keyed on it never holds with TAA on. Key caches on the
  unjittered projection (`get_projection_unjittered`).
- A GPU readback in the frame loop drains the GPU: `hi_res_reflection_pages` (project setting, off by default)
  waits on one every 16 frames.

Shader codegen (the trace kernels are latency and register bound):

- fxc inlines every call site: a helper called twice in a trace kernel doubles its code. Keep one call site
  (near and far as iterations of one loop), keep the card sampling of the hit loop inline (`GiSampleCard`: a
  struct return costs registers), and split a kernel when its temporaries climb (`dcl_temps` in
  `shaderc --disasm`): register pressure, not loads, sets the cost of the hit shading.
- In the large compute kernels a semantically dead edit can change the image through codegen: bisect by
  measurement, keep live ranges short, prefer removing structure to adding it.
- Debug taps live in the debug and visualize shaders (`fs_gi_scene_debug.eshader`, `fs_gi_visualize_screen.eshader`,
  the `visualize` variants), so production shaders compile byte-identical; diff the compiled production shader
  after adding a view.
- Small dispatches into one volume serialize and each pays its own tail: count launches before splitting one
  to rebind a resource.
- Trace kernels compile per ray resolution (the `_res4` / `_res16` variants of `cs_gi_probe_trace.eshader` and
  `cs_gi_integrate.eshader`); suite "gi constants" compiles every GI shader for SM 5.0 and GLSL.

## How it works

Frame order (`pipeline_gi.cpp`):

1. Prepare (`run_gi_scene_passes`): `surface_cache_system::update_world` walks every model, visible or not, into
   the instance list, the SDF atlas residency, the light buffer and the instance cull grid; then the view's
   `surface_cache_view::update` snaps and composes the global SDF (on the GPU in `gi_clipmap_compose_pass`).
2. Lighting (`run_gi_pass`): `run_gi_surface_cache` places, sizes, captures and schedules the cards, then lights
   them (direct, radiosity). `gi_gather_pass::run`: probe placement, adaptive probes, radiance cache update,
   ray generation (importance sampled from last frame's history), the trace (screen, then global SDF far field,
   then hit shading from the cards; past the near field rays hand off to the radiance cache), composite, probe
   filter, SH, border, short-range AO, integrate + temporal. Then `gi_reflection_pass`: screen trace, distance
   field trace, resolve, temporal, spatial + composite over the gather's rough specular.

Layouts (Epic tier, defaults):

| Store | Layout |
|-------|--------|
| Global SDF | 4 levels of 252^3 R8 distances; level 0 spans 50 m (19.8 cm voxels), doubling to 200 m |
| Screen probes | every 16 px (High 32), 8 x 8 rays at quality 1 (4 or 16 per axis by quality), jitter period 8 frames |
| Radiance cache | 4 clipmaps of 48^3 cells (1.04 m at level 0, doubling to 200 m), 32^2 texels per probe (High 16^2), 8192-probe pool, a quarter of the rays past 40 m |
| Cards | 100 texels x half extent / distance, at most 20 / m and 512 texels; 128 x 128 physical pages; 4096 atlas, about 34 bytes per texel |
| Reflections | pixels below roughness 0.4 trace a ray; rougher ones read the gather's rough specular (up to 0.8) |

Update gates (what refreshes each store):

- Global SDF: a level re-snaps with its camera. Edits recompose partial boxes (level 0 every frame, levels 1-3
  staggered: `GI_CLIPMAP_PARTIAL_UPDATES_PER_FRAME`); an edit past `GI_CLIPMAP_MAX_PARTIAL_INSTANCES` or
  `GI_CLIPMAP_MAX_PARTIAL_FRACTION` recomposes the level, at most once per `GI_CLIPMAP_EDIT_THROTTLE_FRAMES`;
  full rebuilds take `distance_field.levels_per_update` levels a frame, finest first.
- Card captures: a card is captured when allocated (new, or its resolution changed). Material edits reach it
  through `render_interest::gi_card_capture` / `gi_card_direct`; per-instance material values recapture at most
  every 30 updates; `card_capture_refresh_fraction` of the capture budget recaptures the oldest pages.
- Card lighting: `gi_scene::schedule_lighting` takes pages by priority bucket (age x speed, speed
  1 / (1 + distance / 25 m), doubled near a frustum) within a tile budget per context (direct about 1/32 of the
  atlas a frame, radiosity 1/64, at lighting update speed 1). Direct lighting keeps no history: a page lit since
  its last invalidation is skipped. `collect_direct_lighting_changes` invalidates a local light's reach before
  and after any byte of its record changes; a directional light, the cloud shadow map, a global lighting change,
  a scrolled or fully recomposed SDF level, or more than 256 moved occluders relight every page. Radiosity
  accumulates up to 4 updates per tile.
- Radiance cache: this frame's screen probes mark the probes they read; a new probe traces the frame it appears;
  150 more (High 100, x update speed in [0.5, 4], x10 while editing) re-trace oldest first; an unused probe keeps
  its slot 8 frames. A global lighting change (sun or sky brightness 4x either way, `has_global_lighting_change`)
  rebuilds it.
- Gather temporal: at most `GI_TEMPORAL_MAX_FRAMES` (10) / sqrt(update speed), halved while editing. Rays whose
  hit moved (screen hits through the velocity buffer, SDF hits through the hit instance's motion in the instance
  table) shorten the history (the fast update). A camera cut or a global lighting change starts it over.
- Reflection temporal: 12 frames, 2 for mirrors.
- A camera without GI releases the surface cache, the radiance cache and its distance field; they rebuild when
  GI returns.

Settings: per volume, `gi_settings` on the Global Illumination component (groups diffuse, reflections,
ambient_occlusion, scene, distance_field; the quality knobs follow UE's quality formulas); per project,
`gi_project_settings` (High / Epic tiers as UE's scalability levels, atlas sizes, card budgets).

## Changing GI

- Port from UE's scalability INI at the target level (BaseScalability.ini), not the CVar default, and cite the
  line. When a change makes the engine do what UE does, rewrite the affected tests to UE's behavior instead of
  bending their inputs. A bias UE's own path shares (the downsampled reflections at quality 0.25 and below read
  darker through the same denoiser-space average) is parity, not a bug.
- The GI converges inside the 10-frame window by cutting per-frame variance; do not lengthen the accumulation.
  An accumulation-length change is the user's call: watch the running image for crawl and hand the user the live
  setting before changing a default.
- A temporal pass's history is its unmodified resolve; display-only steps (sharpening) write a separate target.
- A new setting changes the output on its own at the shipped defaults, or it replaces the overlapping one.
- Price a setting at the settings the user runs (probe spacing, FHD / 4K), not at the code default: cost scales
  with probe count.
- Before proposing anything stable in screen space (anchors, sample positions), estimate its lifetime in frames
  at real camera speeds against the temporal window and state the hop size and rate.
- Before making dead code live, name the behavior that depends on it being dead and measure it; when it is
  load-bearing, show the user both.
- Before an instrument takes a texture lane, list every reader of it, the writing pass's own history read
  included, and every copy between lanes; gate an instrument lane on its view being shown.
- A new lane that gates the temporal is read through the cases where it must be zero (static scene, every
  intensity) before it lands.
- An input that changes every frame by design (a moving light, a mover's motion) gets its own change signal:
  folded into a key that resets global state, it holds every store at its reset.
- A cache invalidated by region covers the reach of every sample its cached value read (an occluder between a
  page and its light; a level scroll changes coverage), with a test that nothing beyond the bound changed.
- Key a remedy on the age that carries the artifact (pixel history, probe state, cache): screen-edge probes
  continue last frame's border probe, so they are never young. Make the selection visible before reading an A/B.
- A per-frame operation on a moving boundary (a reveal feather) applies once per pixel: check slow and fast
  motion for compounding.
- Attribute before fixing: separate the candidate mechanisms with an instrument (answering tier, the trace type
  bit) before writing a guard. An emulation feeds the design its own inputs, not a neighbour's structure. A
  stage-cost variant of an adaptive kernel pins every adaptive input (importance sampling, adaptive probes,
  budgets) in the variant and its base. A neutral verdict gets two diagnostics: the mechanism forced everywhere
  and compiled but never firing, plus a census of how often it fires.
- A change to a refresh cadence (placement, cache budget, card lighting budgets, fast update triggers) or to
  where rays hand off (near field, cache start) passes the sealed-room checks before it lands.
- Suites: "gi constants", "gi bake", "eshader files", "render feature shaders", "shader constant tables"; "render
  change" when capture keys change. Build and test steps: skill `unravel-build-verify`.

## Measuring GI changes

General A/B rules (interleaved builds, the GPU clock): skill `unravel-debugging`. Driving the editor: skill
`unravel-editor-mcp`. The test scenes (Sponza, the GI sample project, the sealed-cell test-suite scene) live in
projects outside this repository: confirm the project, the entity count and one capture of the pose first.

Setup:

- A/B inside one launch with `gi_set_experiment_flags`. The bits are listed in `gi_gather_pass.h`,
  `gi_reflection_pass.h`, `gi_surface_cache.h`, `gi_pass_common.h`, `pipeline_gi.cpp` and
  `surface_cache_system.cpp`; bits 0-44 are taken. Isolating bits: 2 no radiance cache, 16 reflection trace
  types, 24 no adaptive probes, 27 freeze the clipmap origin, 28 hold card resolutions, 29 no gather history,
  30 freeze card lighting, 31 freeze residency.
- Prove the change's code path ran (a counter, or a marker the normal path cannot produce) before reading an
  A/B; a run where it did not is an A/A. Same-launch captures of identical code differ: take the noise floor
  from an A/A.
- A failed shader compile keeps the old binary silently: confirm the compiled binary is newer than the edit and
  the editor log has no compile error before measuring a shader variant.
- Measure the baseline in the same session. Start every capture from a saved settings baseline and restore it;
  read every setting back after writing it (a write to a wrong path succeeds and changes nothing); read a scene
  value before nudging it and restore the value read.
- Across builds: the same project copy, pose, view size (`viewport_set_render_size`) and verified binaries.
- Pin exposure (Auto Exposure min_ev = max_ev) or normalize by the frame mean: the auto exposure follows the GI
  light. Keep total light constant (move an emitter rather than remove it).
- Hide the editor overlays (`viewport_set_overlays`); judge the game's look from the game camera.
- Frame rate: visual and temporal gates with vsync on (57-60 fps, where each frame moves the camera further and
  holds counted in frames last longer); cost with `--novsync`, stating viewport and probe count, judged at FHD /
  4K. `--renderer` takes dx11, dx12, vulkan or opengl (`preferred_renderer_from_string`); an unknown name such
  as d3d11 silently falls back to bgfx's default renderer.
- Per-pass GPU rows (`profiler_get_gpu_passes`) are read parked, as medians: the view layout shifts under motion.

Debug views:

- Print the capture size with every readout; pixel boxes belong to one capture size; derive a tile pitch from
  the capture scale and the probe spacing.
- Report the clipped share inside the mask and read at two scales (`indirect_diffuse` at scale 0.5 or lower).
- The GI views and `indirect_diffuse` go through the lit image's tone mapper: set Tonemapping to None for linear
  values; never sRGB-decode a capture.
- The GI Scene views composite the sky where a ray misses: verify a mask derived from a miss value on an
  open-sky pose. The editor overlay draws over every debug capture.
- Before trusting a categorical view, check what it paints for the states you are not hunting. An instrument
  pin carries a channel only the pinned program can produce, asserted before any number is quoted.
- Compare the same world point at every distance (look_at and a centre box; the normals view confirms the
  surface), and run the pipeline with the cache removed (bit 2) before blaming the cache.
- Screen Probe Frames Accumulated reads the count against the current maximum: full red means the window was
  reached, not convergence.

Metrics:

- Judge a variance reduction by temporal std, not by the mean image.
- Noise under motion is scored against a converged reference at the same pose (park, settle 2.5 s, compare);
  a temporal std under motion measures parallax.
- A census meant to find an error is conditioned on the reference's verdict for the same pixels; a share of rays
  is weighted by the energy it carries.
- A level claim is scored against a brute-force reference of the same model (rays run on to the cascade's
  reach, sky on a miss) at several poses, whole frame and 32 px blocks, never against each variant's own
  converged image.
- Build the mask on the surface the user names (the normals view separates floors from walls).
- Editor captures are about 100 ms apart: per-frame effects need `viewport_measure_temporal` (frame-locked marks,
  scripted motion) or a view of the state in one frame.
- A temporal change passes every motion class before it is reported: rest, pure turn, forward walk, walk plus
  turn, the frames after stopping (+8, +16), a reveal jump; each against the old path in one binary with A/A
  repeats, re-run after every late change. A settle curve comes from one continuous run.
- Measure GI stability with the TAA camera jitter off and judge the lit image too: the Indirect view overstates
  visible GI noise.
- Never score an image filter by "more gradient is better": compare against AA off after a 10 s still settle and
  look at a zoomed still crop.
- Sealed cells: the test-suite scene's thick and 5 cm sealed rooms hold no light. Read them after a fixed wait
  since launch (auto-exposed interiors drain for minutes) in a debug view or an HDR readback, never in the
  tonemapped image. The CPU oracle's sealed-room and thin-wall tests in `gi_oracle.cpp` pin the reference.
- Comparing with UE: brightness at a fixed exposure in both engines (a slowed UE world time freezes its eye
  adaptation); UE's indirect diffuse visualization turns screen traces off, so compare with
  `diffuse.screen_traces` off and judge screen traces in the lit view; UE's visualizations go through its
  upscaler, so compare against its AA-off capture.

What the user judges:

- Capture the user's view at the user's settings before the first fix and after every landing; report that
  first. Component metrics decide what to land, not when the work is done.
- For a visual artifact send frame-exact screenshots of every attempt in the user's configuration (the stop
  frame, its difference from the settled image, the settled view) with the numbers, and keep iterating while the
  user can still see it. A metric equal to a reference configuration only says the cause is shared.
- A metric gain the user cannot see does not justify a default change: report it as data.

## Entry points

| Purpose | Path |
|---------|------|
| Frame driver | `engine/engine/rendering/pipeline/deferred/pipeline_gi.cpp` |
| World state, residency, experiment flags | `engine/engine/rendering/gi/surface_cache_system.h` |
| Card scene (CPU), lighting schedule | `engine/engine/rendering/gi/gi_scene.h`, `engine/engine/rendering/pipeline/passes/gi_surface_cache.h` |
| Screen probe gather, radiance cache | `engine/engine/rendering/pipeline/passes/gi_gather_pass.h`, `engine/engine/rendering/pipeline/passes/gi_radiance_cache.h` |
| Reflections | `engine/engine/rendering/pipeline/passes/gi_reflection_pass.h` |
| Tiers, budgets, shared helpers | `engine/engine/rendering/pipeline/passes/gi_pass_common.h` |
| Constants, settings | `engine/engine/rendering/gi/gi_pass_constants.h`, `engine/engine/rendering/gi/gi_settings.h`, `engine/engine/rendering/gi/gi_project_settings.h` |
| SDF bake, clipmap | `engine/engine/rendering/gi/mesh_sdf_baker.h`, `engine/engine/rendering/gi/global_sdf_clipmap.h` |
| Shader core | `engine_data/data/shaders/gi/sdf_common.sh`, `engine_data/data/shaders/gi/gi_surface_cache.sh`, `engine_data/data/shaders/gi/cs_gi_probe_trace.eshader` |
| Debug views, temporal probe | `engine/engine/rendering/pipeline/passes/gi_visualize_pass.h`, `engine/engine/rendering/pipeline/passes/temporal_probe_pass.h` |
| Tests | `tests/tests/suites/gi_bake.cpp`, `tests/tests/suites/gi_oracle.cpp` |
