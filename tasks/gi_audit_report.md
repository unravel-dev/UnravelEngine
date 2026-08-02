# USC-GI audit

Scope: the world-space surface cache GI system as of branch `feature/gi`, commit `e35a8f1`.
Baseline verified before starting: `build/RelWithDebInfo/bin/gi_tests.exe` -> **4203 checks, 0
failures**. Binaries (00:54-00:55) postdate the newest source (`sdf_atlas.cpp`, 00:54), so nothing
below is a stale-binary artefact. No source was modified during this audit; no instrumentation was
written.

Citations are `file:line`. Every claim is marked CONFIRMED (path read end to end, or demonstrated)
or SUSPECTED (with what would settle it).

**Lessons check.** One lesson is implemented in form but not in effect -- the luminance-moment
companion to the count-driven temporal mean (A3). I would call that a partial regression rather
than a fresh defect, and it is flagged as such. Everything else in `lessons.md` that I could check
statically is intact: `PREV_DEPTH` lifetime is owned by one place
(`pipeline.cpp:797-806`), the pass clears its output on early-out (`pipeline.cpp:2071-2075`),
`get_matrix()` is used for the reprojection uniform (`gi_resolve_pass.cpp:385`), the bounce excludes
its own slot (`cs_gi_cache_update.sc:215-218`) and the receiver deliberately does not, cascade
cross-fade is conservative and fades the reported voxel size (`sdf_common.sh:414-442`),
`is_sampleable` is used on the sampling path, and the duplicated cross-file constants all currently
agree (see B4 for why that is luck rather than a mechanism).

---

## A. Correctness findings

Ranked by impact/effort.

### A1. Surfaces past the cascade's coverage are registered with a fabricated normal at an unaddressable position

**CONFIRMED.** Severity: high (capacity + cost + silent data corruption in the cache).

The insert pass registers G-buffer surfaces out to `insert_max_distance = 200.0f`
(`gi_cache_pass.h:35`). The global cascade covers far less than that: `base_extent = 16.0f`,
`level_scale = 2.0f`, `level_count = 4` (`global_sdf_clipmap.h:57,68,80`) gives an outermost extent
of `16 * 2^3 = 128 m`, centred on the camera (`global_sdf_clipmap.cpp:174-176`), so **coverage is
+/- 64 m**, and `sample_level` further excludes the outer half voxel
(`global_sdf_clipmap.cpp:401-405`, mirrored at `sdf_common.sh:333-336`).

The failure is in the resolve. `SdfResolveSurfacePoint` (`sdf_common.sh:482-510`) initialises
`result.position = world_position` and `result.normal = vec3(0,1,0)` (`:485-486`). Outside every
level, all six gradient taps return `SDF_CLIPMAP_OUTSIDE`, the gradient is exactly zero, the loop
breaks at the first iteration (`:501-504`), and **the function returns its own initialisation** --
the raster position and a hardcoded up vector -- with no way for a caller to distinguish that from
a real answer.

`cs_gi_cache_insert.sc:75-83` then proceeds. Its only guard is
`if(dot(surface.normal, world_normal) < 0.0) return;` (`:78`), which against `(0,1,0)` admits every
upward-facing surface. So for every sampled pixel between ~64 m and 200 m:

- `GiQuantizeNormal(surface.normal)` is `+Y` regardless of the surface's actual facing, so the
  normal-in-key leak defence (design R3, first line of defence) is disabled wholesale out there;
- the key is derived from the *rasterised* position, which is the exact failure
  `lessons.md` ("A cache of the FIELD must be addressed in field space") was written to prevent;
- the entry can never be read, because a clipmap trace beyond coverage returns
  `SDF_CLIPMAP_OUTSIDE`, so `t` jumps past `t_max` and the trace misses (`sdf_common.sh:776-798`);
- the entry is never retired, because the update pass treats a saturated reading as "unknown, leave
  alone" (`cs_gi_cache_update.sc:111-125`) -- correct policy, wrong population;
- and it *is* lit every frame, with a shadow ray per light plus a bounce ray
  (`cs_gi_cache_update.sc:133,141-242`).

There is a second, sharper manifestation just *inside* the boundary. Where some gradient taps land
in coverage and some do not, the difference is ~1e6 over one voxel, so the normal points inward and
the Newton step -- clamped to `voxel_size * SDF_SURFACE_RESOLVE_MAX_STEP` = 4 level-0 voxels = 1 m
per iteration (`sdf_common.sh:455,506-507`) -- drags the point up to 4 m across four iterations. A
shell of misplaced entries forms at the coverage edge.

**Observable symptom.** Not an error. Cache occupancy far higher than the near field justifies, GPU
update time spent on entries that contribute nothing, and -- the part that reaches the image -- the
useful near-field entries pushed into probe-chain contention. `lessons.md` ("An open addressed table
near saturation fails as BOTH writer and reader") describes exactly how that presents: a cache that
looks densely populated and is never hit. Order-of-magnitude for the wasted population, as an
arithmetic estimate rather than a measurement: a flat ground plane between 64 m and 200 m is
`pi*(200^2 - 64^2) ~= 113,000 m^2`, and at `max_level = 3` the cell is `0.25 * 8 = 2 m`
(`radiance_cache.h:57,72`), so ~28,000 cells of pure garbage from the ground alone, against a
524,288-entry table (`radiance_cache.h:47`). Vertical structure multiplies it.

**Fix sketch.** Give `SdfSurfacePoint` a `valid` flag, set false when `SdfFindClipmapLevel` returns
`SDF_CLIPMAP_LEVEL_COUNT` or the gradient is degenerate, and have all three callers
(`cs_gi_cache_insert.sc:75`, `cs_gi_cache_update.sc:165`, `fs_gi_resolve.sc:120`) skip on it. Then
derive `insert_max_distance` and `gi_resolve_pass::max_distance` from
`global_sdf_clipmap::get_level_extent(level_count - 1) * 0.5` rather than letting them be
independent constants that promise range the geometry does not have.

**Test that would have caught it.** The parity test that does not exist -- see A2. Specifically:
assert that `resolve_surface_point` at a position outside every cascade level reports failure rather
than returning the input with an up normal. Debug mode 23 (`cache_slots`, enum value 8) would also
show it as occupancy that does not shrink when the camera looks at a wall.

---

### A1b. `normal_bias` was a world distance where it had to be a voxel count -- MISSED BY THE AUDIT

**CONFIRMED, fixed.** Severity: high; it was the single largest visual improvement found in this
whole effort, and it was found by a user turning a knob rather than by the audit.

`gi_resolve_pass::settings::normal_bias` lifted a gather ray off the surface by an absolute
**0.05 world units**. What that lift has to clear is the trace's hit acceptance, which is
`surface_bias` VOXELS of whichever field answers -- and the cascade's voxel runs from 0.25 m at
level 0 to 2 m at the outer level. So the world distance required varies eightfold across a single
view, and no constant works: measured, 0.1 cleared the acne in a scene contained inside level 0,
while a view spanning levels 1-3 still needed 1.0.

The symptom is surface acne -- rays starting on the isosurface read a distance of zero and report
their own origin as an occluder. It presents as scene-dependent and distance-dependent, so it reads
as a data or content problem rather than as a units problem.

This is `lessons.md`'s own entry, "Thresholds must be expressed in the units of the thing they
judge", which even predicts the misleading presentation. `surface_bias` and `bounce_surface_bias`
were both converted to voxel fractions when that lesson was written; `normal_bias` sat next to them
and was missed, and the audit did not catch it either despite the section explicitly asking for
"anything scaled to a voxel or a cell that does not name WHICH".

Now `normal_bias_voxels`, scaled by the voxel size reported at the shading point. RENAMED rather
than reinterpreted: an existing scene's 0.05 would otherwise silently become a twentieth of a voxel,
which presents as acne rather than as a settings problem.

**Two siblings have the identical defect and are NOT yet fixed**, deliberately, so each can be
attributed on its own:
- `gi_cache_pass::settings::surface_offset` (0.05 world) lifts a cell centre before lighting it. Its
  own doc comment names the failure -- "too small and every shadow ray starts occluded, so the entry
  converges to black" -- and a cell is 0.25 m to 2 m across, so the same eightfold range applies.
  This is a candidate explanation for the large black regions in the cache debug view on Bistro.
- The shadow ray's own normal offset, hardcoded at 0.15 world in `gi_cache_pass.cpp`'s
  `shadow_params`.

### A1c. An exhausted shadow ray was silently reported as LIT -- MISSED BY THE AUDIT

**CONFIRMED, fixed.** Severity: high. Found by following a user's observation that two knobs which
should have been independent were fighting each other.

`GiTraceShadow` passed **zero cone relaxation** with a 48-step budget, and treats a ray that runs
out of budget as fully lit (`gi_lighting.sh`, `return hit.hit ? 0.0 : 1.0`). That last part is a
defensible choice on its own -- over-lighting degrades more gracefully than stamping shadow onto a
region -- but combined with no relaxation it is a trap.

A shadow ray toward a low sun runs nearly parallel to the ground, and a grazing sphere trace
advances by a distance that stays small for its entire length. Those rays exhaust, and exhaustion
was being converted into "lit". The failure therefore does not present as a missing shadow, which
would be recognisable; it presents as ground that is simply too bright, with nothing anywhere
saying a ray gave up. `SdfRayHit::exhausted` exists precisely to distinguish "found nothing" from
"gave up" and no consumer outside the debug view reads it.

What made it hard to diagnose is that it inverted the meaning of the obvious knob. Raising the
shadow normal bias to escape self-intersection pushes the origin further out, which makes rays
graze MORE, which makes exhaustion MORE likely -- so the two failure modes could not be separated
by tuning. Measured: at zero relaxation the ground washed out and no bias fixed it (0 blacked the
cache, 2.0 whitened the image); at 0.1 the wash is gone and the bias drops to 0.35.

Both of the shadow trace's cone parameters were hardcoded, so none of this was testable without a
rebuild. They are now `shadow_surface_bias` and `shadow_step_relaxation` on `gi_component`.

**The general shape is worth carrying:** this is the third ray population found tracing with
relaxation zero, after the near field (fixed earlier, 28% of the gather) and this one. The cone
formulation was introduced for the debug view and never propagated to the rays that graze hardest.
Anywhere a trace can give up, check what its caller does with the give-up case -- here it was
being laundered into a confident answer.

### A1d. GI gets DARKER as the camera approaches a surface

**CONFIRMED by observation, not yet fixed.** Severity: high -- it is backwards, and it is the most
visible remaining defect.

Walking toward a wall that is well lit at cascade 2 makes it darken as it enters cascade 1 and again
at cascade 0. Approaching a surface should improve its GI, never degrade it.

Two independent quantisations both change as the camera moves, and both re-key the surface:

- `GiCacheLevel` is a hard step at `base_distance * 2^k`. Crossing it changes the cell size, so
  every key for that surface changes and the entries built at the previous level are simply not
  found. They are not wrong, they are unreachable.
- The cascade level covering the point changes, so `resolve_surface_point` converges onto a
  different isosurface and moves the derived address again.

A miss contributes nothing and lowers the resolve weight, so the consumer falls back to the
environment probe -- which in a shadowed street is dark. Hence darker, not noisier. The entries do
repopulate, but the cross-fade band measurement explains why it does not fully recover: addressing
agreement inside the band is roughly 20% against 87% well inside a level.

This is D7 (cache-level cross-fade) plus the band finding below, and it is now the strongest
argument for doing them: the design already solved exactly this for the FIELD in Phase 2b, where
switching levels hard put a step in a function two consumers had to agree on. The cache has the same
defect and never got the same fix.

### A1e. Clipmap composition is 4.56 ms of pool-thread work, still on the CPU

**CONFIRMED, measured.** `GI/Clipmap/Compose Level` profiles at 4.56 ms wall with 430 us busy (9%)
on the main thread -- the exact shape `lessons.md` warns about, where a scope wrapping
`poolstl::par` measures the caller BLOCKING and the real work is on pool threads the scope does not
cover. It fires whenever the camera moves far enough to re-snap a level.

`todo.md` item 5 already proposed moving this to compute and deprioritised it on the grounds that
"the whole surface cache update is 2.75 ms". That reasoning no longer holds: the GPU passes are now
~4.4 ms total, so a 4.56 ms CPU hitch on camera motion is comparable to the entire rest of the
system, and it lands as a stutter rather than as steady cost. Moving composition to compute also
removes the per-level upload and is what unblocks resolution 128.

### A2. `SdfResolveSurfacePoint` has no CPU reference and no parity test

**CONFIRMED (by absence).** Severity: high, as insurance rather than as a live defect.

This function *defines the cache address*. Writer and reader finding each other is entirely
contingent on it, and its failure mode is a cache that reads as empty -- which `lessons.md` records
costing multiple debugging rounds under three different root causes.

Every other shared CPU/GPU algorithm in this system acquired a transcription test after it bit:
`test_gpu_addressing_matches_cpu`, `test_cache_shader_transcription_matches_cpu`,
`test_instance_grid_shader_walk_matches_cpu` (`gi_bake_tests.cpp:649,2265,1660`). `lessons.md` calls
this out as "the third time that shape has paid here". Surface resolve is the fourth shared
algorithm and the highest-stakes one, and the test list (`gi_bake_tests.cpp`, 45 tests) contains
nothing for it.

**Fix sketch.** Add `global_sdf_clipmap::resolve_surface_point(world_position)` as the CPU
reference, transcribing `sdf_common.sh:482-510` including the step clamp and the per-level voxel
epsilon. Then two assertions over thousands of start points: (1) the result lies on the isosurface
within a fraction of the answering level's voxel; (2) **two starts a voxel apart on either side of a
surface converge to the same cache cell** -- that second one is the actual contract, and it is the
one that would have caught both A1 and the original field-space addressing bug.

---

### A3. The denoise luminance tolerance never collapses, so a converged image is blurred forever

**CONFIRMED.** Severity: medium-high (permanent, global softening of indirect light). Effort to fix:
one line.

`fs_gi_denoise.sc:91-93`:

```glsl
vec4 moments = texture2DLod(s_gi_moments, uv, 0.0);
float variance = max(moments.y - moments.x * moments.x, 0.0);
float luma_sigma = u_gi_denoise_luma_phi * sqrt(variance) + 1e-4;
```

The moments accumulate the **raw per-frame gather** luminance, not the accumulated estimate:
`fs_gi_temporal.sc:241` takes `luma = Luminance(current.xyz)` where `current` is this frame's 4-ray
sample, and `:246-247` blends `luma` and `luma*luma` into the moments. So `moments.y -
moments.x^2` converges to the **per-sample variance sigma^2**, which is a constant of the scene and
the ray count. It does not decay.

But the value being filtered is a mean of `count` samples, whose standard error is `sigma /
sqrt(count)`. The edge stop should therefore shrink as `1/sqrt(n)`. As written it floors at
`phi * sigma` permanently, so once `count` reaches the cap of 48 (`gi_resolve_pass.h:67`) the
tolerance is about `sqrt(48) ~= 6.9x` wider than the signal warrants, and the a-trous filter keeps
mixing every converged pixel with its neighbours at full strength, every frame, forever.

The comment directly above it (`fs_gi_denoise.sc:81-86`) states the opposite -- "where the temporal
estimate has settled the variance is tiny, the tolerance collapses ... the filter stops re-mixing a
stable value with its neighbours every frame, which is itself a source of shimmer" -- and so does
`lessons.md` ("Where the estimate has settled the tolerance collapses and the filter stops touching
the pixel, which preserves detail AND removes the filter itself as a shimmer source"). **This is the
one place I would call a lesson regressed**: the mechanism was added, but it measures the wrong
quantity, so the property the lesson exists to secure is not delivered.

**Observable symptom.** Indirect diffuse is softer than the sample count justifies, permanently and
everywhere. Contact darkening and colour-bleed boundaries wash out even with the camera held still
for seconds. It reads as "indirect light is inherently low frequency", not as a filter bug -- which
is why it survives.

**Fix sketch.** `count` is already read two lines later for the low-count boost
(`fs_gi_denoise.sc:100`). Move it up and divide:

```glsl
float count = max(moments.z, 1.0);
float luma_sigma = u_gi_denoise_luma_phi * sqrt(variance / count) + 1e-4;
luma_sigma *= max(u_gi_denoise_low_count_boost / count, 1.0);
```

Keep the boost: it covers freshly disoccluded pixels where a one-sample variance is meaningless.

**Test / view that would have caught it.** Hold the camera still and plot `sqrt(variance)` against
frame index at a fixed pixel -- it is visibly flat where the comment predicts a decay. As a
regression test: render N frames static, measure the mean absolute difference between the denoised
and un-denoised buffers, and assert it decays with `count`. Per `lessons.md`, measure the broken
behaviour first and place the bound between the two numbers, asserting both sides.

---

### A4. Skinned meshes: `asset_storage.h` claims per-segment proxies; there are none

**CONFIRMED.** Severity: medium-high in character scenes; the doc comment actively misleads.

`asset_storage.h:91-93` states: *"The field is rigid, so it is generated for every mesh; skinned
meshes use it through per-segment proxies rather than as a single instance."*

There is no skinning code anywhere in `engine/engine/rendering/gi/` -- no bone, segment, or skin
identifier appears in the directory outside two unrelated comments in `mesh_sdf_source.cpp:126,161`.
`surface_cache_service::update` (`surface_cache_service.cpp:336-410`) takes `mdl.get_lod(0)`, walks
`get_sdf_count()` submeshes, and places each at either its mapped node transform or the entity's
transform. That is the *non-skinned* half of `model::submit`'s branch only; the renderer's skinned
path is separate and palette-driven (`model.cpp:873-980`,
`get_skinned_submeshes_indices`/`skinning_transforms`).

This is the third instance of the shape `lessons.md` names twice ("Any pass that places data
alongside rendered geometry must reproduce that branch rather than invent a paraphrase of it"). The
previous two -- the whole-mesh field placed once per transform, and the outer-emptiness test that
made primitives vanish -- were both found the same way.

**Observable symptom.** A character occludes and bounces light from its **bind pose**, at the model
transform, frozen, regardless of animation. A running character casts a stationary T-pose indirect
shadow; raising an arm does not move its bounce. Because the bind-pose field is a plausible-looking
blob roughly where the character is, this does not read as "GI is broken" -- it reads as GI being
soft and slightly wrong around characters.

**Fix sketch, bounded.** The honest short-term change is to stop claiming it: either skip submeshes
the renderer draws through the skinned path (so a character contributes nothing rather than
something wrong), or register a single OBB proxy at the animated bounds, which
`model.cpp:27-33` already caches (`get_skinned_bounds`). Correct the `asset_storage.h` comment
either way. Per-segment proxies are a real feature and belong in the backlog, not in a doc comment.

**Test.** A fixture with a skinned submesh asserting that the instance count and transform the
surface cache registers match what `model::submit` would draw -- the same cardinality assertion
shape as `test_submesh_extraction_selects_only_its_own_submesh`.

---

### A5. CPU/GPU eviction divergence, and a fresh claim that does not initialise `frame_touched`

**CONFIRMED** (divergence and uninitialised field). **SUSPECTED** (impact: probably none today).

Two coupled issues in the structure the tests are supposed to pin.

*Divergence.* CPU (`radiance_cache.cpp:220`): `if(oldest_slot == invalid_slot || oldest_frame ==
frame)`. GPU (`radiance_cache.sh:324`): `if(oldest_slot == GI_CACHE_INVALID_SLOT || oldest_frame >=
frame)`. `==` versus `>=`.

*Uninitialised field.* `GiCacheInsert` initialises RADIANCE, ALBEDO and EMISSIVE on a fresh claim
(`radiance_cache.sh:301-303`) but **not POSITION** -- and POSITION.w is where `frame_touched` lives
(`radiance_cache.sh:310`, `radiance_cache_gpu.h:30`). Between a thread claiming a slot and the
caller writing position (`cs_gi_cache_insert.sc:102-103`, `cs_gi_cache_update.sc:184-185`), another
thread probing the same chain reads whatever the allocation held. `lessons.md`'s own rule --
"Whoever CLAIMS a slot owns initialising its whole payload", written after exactly this class of bug
with albedo and emissive -- names the invariant, and POSITION is the one field the claim still
skips.

The two interact: the GPU's `>=` is what makes a garbage-large frame get refused as an eviction
victim. So the GPU is defensively correct and **the CPU reference is the one that is wrong**, which
inverts the intended relationship -- the CPU is what the tests pin.

**Observable symptom.** None. That is the point; this is exactly the class of defect this audit is
most useful for.

**Fix sketch.** Write `b_gi_cache_data[GiCacheDataIndex(slot, GI_CACHE_DATA_POSITION)] =
vec4(0,0,0,float(frame));` in the fresh-claim branch, and change the CPU guard to `>=`.

**Test.** Extend `test_cache_insert_find_and_evict` (`gi_bake_tests.cpp:2095`) with a victim whose
`frame_touched` is in the future, asserting it is refused.

---

### A6. `frame_touched` is refreshed only by the insert pass, so offscreen entries are always the preferred eviction victims

**CONFIRMED** (mechanism). **SUSPECTED** (severity -- depends on table pressure, which A1 inflates).

`cs_gi_cache_insert.sc:102-103` rewrites POSITION.w every frame for cells a screen pixel resolves
to. The update pass never does: it lights the entry and writes only RADIANCE
(`cs_gi_cache_update.sc:264-273`), and the bounce path reads a hit entry's radiance
(`:236`) without touching its age. `GiCacheInsert` also returns early on `previous == key`
(`radiance_cache.sh:306-309`) without refreshing, so a repeat claim does not either.

So "last touched" actually means "last seen on screen". Under probe-chain contention the eviction
victim is preferentially the entry that has been offscreen longest -- which is precisely the
population a world-space cache exists to hold, and precisely what delivers R2.

Note the CPU reference *does* refresh on a key match (`radiance_cache.cpp:198`), so this is also a
second CPU/GPU behavioural divergence, in the same function as A5.

**Observable symptom.** Offscreen contribution degrades as table pressure rises, and it degrades
invisibly until you turn around -- the geometry behind you has been evicted and has to re-converge,
which reads as the temporal ramp-in that R1 says should not happen for already-seen surfaces.

**Fix sketch.** Refresh POSITION.w in the update pass for every entry it lights, and for any entry a
bounce ray reads. That makes age mean "last useful", which is what the eviction policy assumes.

---

### A7. Emissive surfaces self-illuminate through the receiver-side gather

**SUSPECTED.** Severity: low. Bounded, not divergent.

`fs_gi_resolve.sc:120-131` deliberately does not exclude the shading point's own cell, and
`lessons.md` is right that the receiver needs no exclusion -- no loop closes, and excluding it would
discard legitimate near-field bounce off the same wall. For a non-emissive surface that is correct.

For an emissive one it is not quite. The cache stores outgoing radiance including emission
(`cs_gi_cache_update.sc:264`: `albedo * irradiance / GI_PI + emissive_data.xyz`), so a grazing ray
that resolves back to the shading point's own cell returns that surface's own emission. The consumer
then multiplies it by the receiver's diffuse colour (`fs_pbr_lighting.sh:532,537`) and adds it on
top of the G-buffer emissive the deferred pass already contributed.

**Observable symptom.** Emissive surfaces read slightly brighter than authored and faintly tinted by
their own albedo, with a soft self-halo on large emitters. Easy to mistake for bloom tuning.

**What would settle it.** Debug mode 7 (`cache`, enum value 7) on a large emissive plane: compare
the gathered value at grazing incidence against normal incidence. If the grazing value carries the
plane's own emission, it is confirmed. If it is confirmed, the cheap fix is to skip a gather hit
whose resolved slot equals the shading point's own slot *only when that entry's emissive is
non-zero*, which preserves the legitimate same-wall bounce.

---

## B. Inconsistencies and design smells

**B1. Settings promise range the geometry cannot deliver.** `gi_resolve_pass::settings::max_distance
= 200.0f` (`gi_resolve_pass.h:38`), `insert_max_distance = 200.0f` (`gi_cache_pass.h:35`), and
`sdf_debug_pass::settings::max_distance = 500.0f` (`sdf_debug_pass.h:79`) all exceed the cascade's
+/- 64 m by 3-8x. Nothing derives them from `global_sdf_clipmap::get_level_extent`. A gather ray
past 64 m is not wrong (it misses cheaply and lowers the resolve weight, falling back to the
environment probe -- the conservative direction), but the number in the inspector is fiction, and
for the insert path it is A1.

**B2. Two constants for one concept.** `SDF_CLIPMAP_OUTSIDE 1e6` (`sdf_common.sh:314`, mirroring
`global_sdf_clipmap::outside_distance`) and `GI_SDF_NO_COVERAGE 1e5`
(`cs_gi_cache_update.sc:56`). The entry-retirement path compares against the second
(`cs_gi_cache_update.sc:114`). Lowering the first below the second would silently start deleting the
entire far field -- the exact failure `lessons.md` ("Parameters two passes must agree on need ONE
owner") was written about, and here it is a threshold rather than a key parameter, so the existing
single-owner mechanism on `radiance_cache_gpu` does not cover it.

**B3. Dead uniform and dead sampler in the temporal pass.** `u_gi_normal_threshold`
(`fs_gi_temporal.sc:45`) is never read -- the normal test was correctly removed (`:213-223`) -- and
the C++ side sets its slot to a hardcoded `0.0f` with a comment saying so
(`gi_resolve_pass.cpp:388-392`). Separately `SAMPLER2D(s_gi_normal, 4)` (`fs_gi_temporal.sc:35`) is
declared and bound (`gi_resolve_pass.cpp:378`) and never sampled. Harmless, but it is a binding a
reader will assume is load-bearing.

**B4. The parity tests pin the algorithm, not the constants.** `GI_CACHE_DATA_STRIDE`,
`SDF_INSTANCE_STRIDE`, `SDF_HEADER_STRIDE`, `GI_CACHE_PROBE_LENGTH`, `SDF_CLIPMAP_LEVEL_COUNT`, and
the brick size/border/stride/encode-range set are each duplicated between a C++ header and a `.sh`,
with a comment at both sites saying they must match. **I checked every one; they all agree today.**
But the tests transcribe the shader by hand rather than reading it, so editing only the `.sh` would
still produce 4203 passing checks -- and `lessons.md` records `GI_CACHE_DATA_STRIDE` drifting from 3
to 5 and rendering as saturated primaries. A comment is not a mechanism, which is the lesson's own
phrasing. Cheap fix: have `gi_tests` parse the `.sh` files for those `#define`s and assert them
against the C++ constants. Half a day, and it closes the whole family permanently.

**B5. Two debug numbering schemes coexist undocumented.** The enum is 0-11
(`sdf_debug_pass.h:26-66`); `tasks/lessons.md` cites modes 18, 23, 25, 26, which are `debug_pass_`
values offset by `debug_pass_sdf_normals = 15` (`pipeline.h:120`, used at `pipeline.cpp:781`). Both
are correct; nothing says which is which.

**B6. The 2.75 ms figure is CPU-only and is being read as the system's cost.** `todo.md:109-111`
concludes GPU clipmap composition is "no longer urgent: the whole surface cache update is 2.75 ms".
That scope is `APP_SCOPE_PERF("GI/SurfaceCache/Update")` (`surface_cache_service.cpp:328`), which
covers instance rebuild, uploads and CPU composition. The GPU passes carry
`APP_SCOPE_PERF("Rendering/GI/Cache Pass")` (`gi_cache_pass.cpp:25`) and `"Rendering/GI/Resolve
Pass"` (`gi_resolve_pass.cpp:110`), both of which measure *command submission*, not GPU execution.
So the 2.75 ms is not this system's cost and should not be quoted as it.

**CORRECTION (post-audit).** The original text here and in D5 said no GPU timing existed. That was
wrong, and the error was mine: I inferred it from the absence of timing code in the GI passes
without searching the editor for an existing facility. A complete per-view GPU profiler is already
implemented -- `draw_gpu_submit_profiler_ui` in
`editor/editor/hub/panels/profiler_panel/gpu_frame_stats_widgets.cpp:442`, reached from
`profiler_timeline_panel.cpp:1090`. It toggles `BGFX_DEBUG_PROFILER`, reads `stats->viewStats`, and
reports CPU-submit and GPU-execute milliseconds per view with percentages and bars. It groups views
by the `/` prefix in their names, and every GI pass is already named for it
(`GI/Cache Insert`, `GI/Cache Update`, `GI/Resolve Pass`, `GI/Temporal Pass`, `GI/Denoise Pass`,
`GI/Upsample Pass`), so they collapse under one "GI" group with a subtotal. Compute views are timed
too: bgfx's `Profiler::begin/end` wraps the submit loop on view change
(`renderer_d3d11.cpp:5935`), which covers compute items as well as draws.

The real gap was never the tooling; it was that nobody had switched it on and written the numbers
down.

**B7. Design-doc drift, and one name that means two different things.** Absent from the
implementation but not listed as open in `todo.md`: the bounded per-frame cell selection and
indirect dispatch (design 6.4), invalidation-on-change and priority feedback (6.4, the R4
mechanism), screen probes and adaptive placement (7.1-7.3), and the short SDF visibility check
between probe and pixel (7.5, the third leak defence). Material voxels (4.2) are absent
*deliberately* and that is documented (`todo.md` Phase 4b) -- good. Most notably, **`near_field_*`
means two different things**: the design's `near_field_distance` is 1.5 m, the screen-space to
mesh-SDF crossover (design 9.2); the code's is 30 m, the per-instance to cascade crossover
(`gi_resolve_pass.h:47`, `gi_cache_pass.h:59`, `sdf_debug_pass.h:93`). Anyone reading the design and
then the code will mis-set it.

---

## C. Comparison with production systems

| Axis | Them | Us | Why it matters here | Worth adopting? |
|---|---|---|---|---|
| Radiance store | Lumen: card atlas per mesh, needs capture + defrag | World hash, age-evicted, no defrag (`radiance_cache.h:26-28`) | Ours is genuinely simpler and content-independent -- no card layout artefacts, works for procedural/instanced geometry unchanged | **Already better.** Keep. |
| Cache addressing | Lumen: card UV, exact by construction | Field-space isosurface resolve (`sdf_common.sh:482`) | Ours has no CPU reference or parity test for the one function both sides depend on | **A2. Yes, high priority.** |
| Update budget | Lumen: fixed per-frame surface-cache radiosity budget, indirect dispatch | Full sweep of all 524,288 slots every frame (`gi_cache_pass.cpp:168`) | This is almost certainly our dominant GPU cost, and it is unbounded in scene size | **D1. Yes -- biggest single win.** |
| Final gather | Lumen: screen probes + adaptive placement; ours: per-pixel rays into the cache | 4 rays/pixel at half res (`gi_resolve_pass.h:36,55`) | Probes buy sharper contact detail at lower cost, but they are a large subsystem | Defer. Our per-pixel gather at half res is a reasonable point on the curve; revisit after D1. |
| Level transitions | Godot SDFGI and Lumen both cross-fade cascade *and* probe LOD | Field cascade cross-fades (Phase 2b, `sdf_common.sh:414-442`); **the cache level does not** (`radiance_cache.sh:148-158`) | The same argument that justified Phase 2b for the field applies unchanged to the cache | **D7. Yes -- the analysis is already written, just not applied to the second structure.** |
| Visibility for leak control | DDGI/RTXGI: per-probe Chebyshev depth moments | Normal-in-key + per-instance near field + cell size (`radiance_cache.h:60-72`) | Chebyshev is the standard answer to leaking and would be a fourth defence; ours currently pays for leak control in cell size, i.e. in table capacity | Worth a spike, but our three defences are coherent. Medium priority. |
| Sampling | RTXGI/Lumen: blue noise / low-discrepancy, often Owen-scrambled Sobol | PCG hash of pixel/slot x frame (`fs_gi_resolve.sc:96-106`, `cs_gi_cache_update.sc:146-153`) | White-noise spectra are the worst case for a temporal filter to remove | **D6. Yes, bounded and measurable.** |
| Bake caching | Lumen: DDC-cached, shared across a team | Per-asset compiled output keyed by format version (`asset_extensions.h`) | Ours is correct in kind; the version-bump discipline is documented and followed | Fine as is. |
| HW ray tracing | Lumen/RTXGI: HW RT fallback for near-field detail | Software SDF only | **Out of scope for now** -- the vendored bgfx exposes no acceleration-structure or ray-query API, so this is a dependency change, not a feature. |
| Card rasterisation | Lumen: full mesh-card capture pipeline | Per-instance material on the SDF hit (`sdf_common.sh:136-138`) | **Out of scope for now** -- our one-material-per-submesh mapping already gives exact attribution for the near field at a fraction of the machinery. |
| Spatiotemporal resampling | ReSTIR GI | Count-driven temporal mean + a-trous | **Out of scope for now** -- reservoir resampling is a large, subtle rewrite of the gather, and A3 says our existing filter is not yet delivering what it was designed to. Fix that first. |

---

## D. Improvement backlog

### D1. Budget the cache update dispatch
- **Axis:** runtime cost. Almost certainly the dominant GPU cost of the system.
- **Today:** `gfx::dispatch(pass.id, ..., (cache.get_capacity() + 63u) / 64u, 1, 1)`
  (`gi_cache_pass.cpp:168`) launches **524,288 threads every frame** (`radiance_cache.h:47`). Empty
  slots early-out on one buffer read (`cs_gi_cache_update.sc:86-89`), but every *occupied* entry
  does a clipmap validation sample, then `GiEvalDirectLighting` -- **one shadow ray per light**
  (`gi_lighting.sh:88-96`) -- then a bounce ray (`:141-242`). Each ray runs up to 48 steps
  (`gi_cache_pass.h:60`), and the near-field instance tier spends that budget **per instance**
  (`sdf_common.sh:633`), not per ray. Design 6.4 specifies a bounded selection of 8192 cells into an
  indirect dispatch; it was never implemented and is not listed as open.
- **Expected win and how to measure:** measure before choosing a number -- `lessons.md` is explicit
  that predicted speedups here have been wrong by an order of magnitude twice. Add bgfx GPU timer
  queries around the two dispatches (D5), and add an occupancy counter so cost per occupied entry is
  separable from cost per slot. Then compare an 8192-cell budget against the full sweep at equal
  image quality.
- **Effort:** 1-2 days (occupancy compaction pass, priority ordering, indirect dispatch args).
- **Risk / what could regress:** cells that lose the budget converge more slowly. Use the design's
  priority -- screen-touched first, then descending age -- and pair with A6, or offscreen entries
  will both starve the budget and lose eviction.

### D2. Normalise the denoise variance by the sample count
- **Axis:** quality at equal cost. This is A3.
- **Expected win:** the luminance edge stop tightens by up to `sqrt(48) ~= 6.9x` on converged
  pixels, so the filter stops re-mixing settled values. Measure as mean absolute difference between
  denoised and un-denoised output over frame index with a static camera -- it should decay, and
  today it does not.
- **Effort:** ~1 hour including the test.
- **Risk:** over-sharpening where the count is high but the underlying signal genuinely changed. The
  history clamp (`fs_gi_temporal.sc:225-231`) already bounds that, and the low-count boost covers
  disocclusion.

### D3. Stop registering entries outside cascade coverage
- **Axis:** runtime cost, cache capacity, and correctness. This is A1.
- **Expected win:** removes a population I estimate at tens of thousands of entries in an outdoor
  scene (arithmetic in A1), each of which currently costs a validation sample, a shadow ray per
  light and a bounce ray per frame. Measure with an occupancy counter before/after, and with the
  cache-slots view (mode 23).
- **Effort:** hours. Add a `valid` flag to `SdfSurfacePoint`, guard three call sites, derive the two
  distance settings from the cascade extent.
- **Risk:** low. Entries that stop being created were never readable. Watch that the near field does
  not lose entries at the coverage boundary -- the flag must be "no level covers", not "the gradient
  was small".

### D4. Break the instance-grid DDA once the walk passes the nearest hit
- **Axis:** runtime cost, every tracing pass (gather, bounce, shadow).
- **Today:** `SdfTraceInstances` (`sdf_common.sh:728-752`) walks cells until `t_step > t_exit` even
  after `result.hit` is set. Instances behind the hit do reject cheaply, because the broad phase is
  capped by `min(result.t, t_max)` (`:611-612`), but the walk still visits up to
  `SDF_GRID_MAX_STEPS = 256` cells (`:80`) with two buffer reads each.
- **Fix:** after `float t_step = min(t_next.x, min(t_next.y, t_next.z));` (`:738`), add
  `if(result.hit && t_step > result.t) break;`. Correct because the walk proceeds in increasing `t`
  and the current cell has already been tested.
- **Expected win and how to measure:** `SdfRayHit::steps` is already tracked and debug mode 1 is a
  step heat map; add a cells-visited counter alongside it for the measurement. Unknown magnitude --
  it depends on how far hits sit from `t_exit` -- which is exactly why it should be measured rather
  than estimated.
- **Effort:** <1 hour. **Risk:** essentially none; must be `>` not `>=`, and must not fire before
  the current cell's instances are tested.

### D5. Record the GPU numbers -- the tooling already exists
- **Axis:** prerequisite for D1 and for every runtime claim in this system.
- **Originally written as "get any GPU timing at all", estimated at half a day. That was wrong.**
  The facility is already built and complete (see the correction under B6). No code is needed.
- **Procedure:** open the Profiler panel, tick `Enable` next to "View/encoder timing", filter the
  render-pass table by `GI`. That gives GPU-execute milliseconds per pass with a group subtotal,
  which is the entire measurement D1 was blocked on.
- **What to record:** the split between `GI/Cache Update` (expected to dominate -- it is the
  unbudgeted 524k-thread dispatch with a shadow ray per light per entry) and the screen-space chain
  (`Resolve` + `Temporal` + `Denoise` + `Upsample`). D1 is only worth its effort if Cache Update is
  actually the larger share; if the resolve chain dominates instead, ray count and trace resolution
  are the cheaper levers and D1 drops down the list.
- **Effort:** minutes. **Risk:** none.
- **Lesson for the audit method itself:** "no instrumentation exists" is a claim about absence, and
  absence has to be established by searching, not inferred from the code under review. Two items in
  this report were wrong in the same direction -- this one, and D10, where I catalogued how the
  cache is sampled without ever asking whether it is filtered. Both came from reasoning about what
  the GI files contained rather than checking what the rest of the tree already provided.

### D6. Low-discrepancy sampling for the gather and the bounce
- **Axis:** quality at equal cost.
- **Today:** both use PCG hashing with 16-bit uniforms (`fs_gi_resolve.sc:96-106`,
  `cs_gi_cache_update.sc:146-153`). White-noise spectra are the hardest case for a temporal filter.
- **Proposal:** an Owen-scrambled Sobol or spatiotemporal blue-noise mask indexed by
  `(pixel, frame mod N)` for the gather; for the bounce, a per-slot Halton sequence advanced by the
  entry's own **sample count** rather than the frame index -- which is additionally *world-stable by
  construction*, satisfying design 6.4's requirement that the seed derive from the cell, not from
  anything camera-dependent. The current `GiHashCombine(GiHashUint(slot), u_gi_cache_frame)` already
  meets that; the Halton version would meet it and converge faster.
- **Measure:** RMSE against a long-accumulated reference at fixed frame counts (16, 64, 256).
- **Effort:** 1 day. **Risk:** entries sharing a sample count correlate -- decorrelate the sequence
  offset with the slot.

### D7. Cross-fade the cache LEVEL, as the cascade already cross-fades
- **Axis:** temporal artefacts (popping at cache-level boundaries).
- **Today:** `GiCacheLevel` is a hard step at `base_distance * 2^k`
  (`radiance_cache.sh:148-158`, CPU `radiance_cache.cpp:120-131`). Design 6.1 explicitly calls for
  allocation at two adjacent levels in a transition band, blended by distance -- not implemented.
  `test_cache_level_is_stable_within_a_band` (`gi_bake_tests.cpp:2072`) pins that transitions are
  *few*, not that crossing one is *smooth*.
- **Symptom:** a surface crossing a level boundary switches to a differently sized cell and its
  radiance restarts from whatever that cell holds -- a step in indirect light at a fixed distance
  from the camera, which travels with the camera. Structurally identical to the cascade hard-switch
  that Phase 2b fixed for the field; the cache still has it.
- **Measure:** walk a camera toward a uniformly lit wall and plot the gathered value at a fixed
  world point across the boundary; assert the step is under a bound, with the pre-fix value measured
  first.
- **Effort:** 1 day. **Risk:** doubles insert cost inside the band; the band should be narrow.

### D8. Invalidate on light and geometry change (R4)
- **Axis:** temporal response to change; this is design 6.4 mechanism 1, absent.
- **Today:** `gpu_light_buffer` rebuilds every frame with no change detection (no dirty/version
  state in `gpu_light_buffer.{h,cpp}`), so lighting is never stale -- but nothing accelerates
  convergence either. The only response is the cache EMA floor `min_alpha = 0.05` with
  `max_samples = 32` (`gi_cache_pass.h:41-43`), then the screen mean capped at 48 frames
  (`gi_resolve_pass.h:67`). From those constants I estimate 1-3 s to converge a light switch --
  against the design's R4 target of 90% within 8 frames near field. The history clamp
  (`fs_gi_temporal.sc:225-231`) pulls this in by an unknown amount, which is exactly why it needs
  measuring rather than estimating.
- **Measure:** toggle a light at frame N and read back a probe cell's radiance per frame. That
  number is currently unknown and should be established before anything is tuned.
- **Effort:** 1 day for a light-set fingerprint that resets `sample_count` for entries inside a
  changed light's bounds.
- **Risk:** an over-broad invalidation is a full re-converge; scope it to the light's range, and
  reuse the per-level fingerprint pattern already proven on the cascade (`todo.md` Phase 2d).

### D10. Cache spatial interpolation -- MISSED BY THE ORIGINAL AUDIT
- **Axis:** quality, and temporal artefacts. Added after fixing D2 exposed it.
- **What the audit got wrong:** the report noted the cache level is a hard step (D7) and that
  sampling is hash-based (D6), but never asked the more basic question -- whether the cache is
  interpolated *at all*. It is not. Every consumer point-samples exactly one cell
  (`GiCacheFindSurface`), and a cell is 0.25 m at base and 2 m at `max_level`, against a pixel of
  millimetres. The gather is therefore piecewise constant at cell scale.
- **Why it hid:** the a-trous luminance stop was so wide (D2) that it blurred the cell steps away.
  Fixing D2 made the filter respect edges, and cell boundaries look exactly like edges to it, so
  the blocks became visible. That is D2 working correctly and revealing a separate defect, not D2
  regressing.
- **Why it is bias, not noise:** the cell a pixel lands in is a deterministic function of world
  position, so temporal accumulation converges TO the blocky answer rather than averaging it out.
  Nothing downstream can remove it: a luminance edge stop cannot distinguish a cell boundary from a
  real lighting discontinuity, so any tolerance wide enough to remove the blocks also removes
  genuine detail. Lumen, DDGI/RTXGI and Godot SDFGI all interpolate between probes for this reason.
- **What was tried and rejected:** jittering the lookup within the cell and letting the temporal
  filter integrate it. Costs no extra lookups, and it does work on a still camera -- but it
  converts the blocks into shimmer, and it is worst while the camera moves, which is precisely when
  disocclusion has reset the accumulation count and there are no frames to integrate over. Measured
  by eye against the point-sampled build, this was worse overall. Recorded because it is the
  obvious cheap idea and it should not be re-tried.
- **Implemented:** `GiCacheGatherSurface` in `radiance_cache.sh` -- deterministic bilinear over the
  four cells bracketing the hit in its TANGENT plane. Tangent-only is load bearing: blending along
  the normal would mix the two sides of a thin wall, which is the leak the normal-in-key defence
  exists to prevent. Weights are renormalised over the taps that resolved, so a neighbour the cache
  has not reached hands its weight over rather than dragging the result to black.
- **Cost, measured:** **0.84 ms**, about 10% of the Resolve pass (9.367 ms with, 8.530 ms without,
  Bistro, 120-frame means at a fixed viewpoint, with the small passes stable across both runs to
  rule out global drift).
- **It is worth far more than it costs, and not for the reason it was added.**
  `test_surface_resolve_addresses_one_cell_from_both_sides` measures how often a writer and a
  reader starting a voxel apart address the same data: **raw 19.7% -> resolve_surface_point 49.6%
  -> inside the gather's 2x2 bracket 87.0%**. So interpolation roughly HALVES the cache miss rate
  for gather rays, from ~50% to ~13%, and every one of those misses was a ray falling back to the
  environment probe. It is a correctness and quality feature that happens to also remove the
  blocks.
- **Why single-cell agreement stalls near half**, which is the finding underneath that number: a
  Newton step travels along the gradient, so `resolve_surface_point` removes the disagreement
  NORMAL to the surface and none of the tangential displacement a grazing sphere trace accumulates
  by stopping short along its ray. `lessons.md` predicted exactly this -- "widening the cell or
  probing neighbours only reduces the miss rate in proportion to error/cell_size and never removes
  it, because the error is tangential as well as normal" -- and nothing had ever addressed it. The
  bracket is the first thing that does.
- **Risk:** cost only. `interpolate_cache = false` restores the point lookup for comparison without
  a rebuild.

### D9. Bake time -- what is left
- **Axis:** iteration speed. Lower priority: the measured numbers are already good.
- **Measured, from this run of `gi_tests`:** 64 submeshes 24.5 ms (0.382 ms each); 4x the submeshes
  cost 3.70x the time (linear, as pinned). Cost is voxel-dominated: 16x the triangles cost 3.67x the
  time on an identical grid, consistent with the ~sqrt(T) closest-point scaling `todo.md` records.
  Threading pairing is worth 16x: 401.8 ms single-threaded, 25.1 ms submeshes-in-parallel.
- **What is left:** the remaining term is exact closest-point queries over the whole grid. A narrow-
  band-plus-propagation scheme (exact distances only within `encode_range` voxels of a triangle,
  then a fast sweep for the rest) would attack the voxel term directly, which is the dominant one.
- **Effort:** 2-3 days and genuinely risky -- it changes bake OUTPUT, so it needs a
  `get_format_version<mesh>()` bump (`asset_extensions.h`) and must be checked against
  `test_field_is_conservative` and `test_sphere_accuracy`. **Not worth it yet.** Bake time is not
  currently the bottleneck; D1 and D5 are.

---

## Start here

Three items, best value to effort, landable together in about a day with tests that prove each:

1. **D2 -- divide the denoise variance by the accumulated sample count** (`fs_gi_denoise.sc:93`).
   One line. It is the only place I found where a recorded lesson is not actually delivering its
   property, and the payoff is sharper indirect light everywhere, permanently.
2. **D3 -- stop registering cache entries outside cascade coverage** (A1). A few hours. It removes a
   large population of unreadable, un-retirable entries that currently consume table capacity *and*
   a shadow ray per light per frame each, and it fixes a real correctness hole (`+Y` face for every
   distant surface) at the same time.
3. **D4 -- break the instance-grid DDA past the nearest hit** (`sdf_common.sh:738`). One line,
   benefits every tracing pass in the system.

Two more that are not in the top three only because they cost more than a day, but which I would
schedule immediately after:

- **D5 then D1.** Record the GPU numbers first, then budget the update dispatch. D5 turned out to
  need no code at all (see its entry), so this is minutes of work, and it decides whether D1 is
  worth its 1-2 days or whether the resolve chain is the real cost instead. D1 is the largest
  single runtime win *if* the cache update dominates -- and sizing it without measuring is
  precisely the trap `lessons.md` documents twice.
- **A2.** A CPU reference and parity test for `SdfResolveSurfacePoint`. It is insurance rather than
  a fix, but it guards the one function the entire cache's correctness rests on, and the same
  investment has already paid three times in this system.

### Status of this shortlist

All three are done, along with two items the audit did not originally contain.

| Item | State |
|---|---|
| D2 -- denoise variance normalised by sample count | done |
| D3 / A1 -- no cache entries outside cascade coverage | done, and B2 folded in |
| D4 -- instance-grid DDA stops past the nearest hit | done, with `test_instance_grid_walk_stops_past_the_nearest_hit` (measured 1.99x fewer candidate visits, 0 missed) |
| D10 -- cache spatial interpolation | done; not in the original audit, found by fixing D2. Costs 0.84 ms, halves the gather miss rate |
| D5 -- GPU timing | no code needed; the audit was wrong, the facility already existed. Averaging was added to it, because single-frame readings could not resolve the differences being chased |
| A2 -- surface resolve CPU reference + parity tests | done; `resolve_surface_point` on `global_sdf_clipmap`, pinned by two tests |
| Skip unused hit normals in the tracers | done; the gather, bounce and shadow rays discarded a 6-sample gradient on every hit |

### What the measurements changed

**D1 is demoted.** Measured on Bistro at a fixed viewpoint over 120-frame means, GI is ~90% of
visible GPU cost, and within it `Resolve Pass` is 57-63% against `Cache Update` at 30-35%. Budgeting
the update dispatch was the report's largest proposed win; it is now the second-largest at best, for
the harder and riskier subsystem. The Resolve pass is also nearly flat in scene complexity
(2.66 ms simple scene, 5.66 ms Bistro) where Cache Update scales 5.5x, which says Resolve is
dominated by FIXED per-ray work rather than by geometry.

**Where that fixed cost is.** Each gather ray spends 28 cascade samples in `resolve_surface_point`
(4 iterations x 7), roughly 112 per pixel at 4 rays. The two obvious cuts -- halving the iterations,
or forward differences instead of central (3 samples rather than 6) -- both change where writer and
reader converge, which fails silently as a cache that stops hitting. A2 exists so those can now be
tried as measured experiments: the parity test reports agreement and bracket-coverage rates
directly, so a regression shows up as a number rather than as a dark image.

**Resolve iterations cut 4 -> 2, at zero quality cost, measured.** `surface_resolve_steps` was 4 on
the reasoning that "a single Newton step lands somewhere that still depends on where it started".
That justified more than one; it never justified four. Sweeping the count against writer/reader
addressing agreement gives bracket coverage of **48.9% / 53.3% / 52.9% / 52.8%** for 1 / 2 / 3 / 4:
one is measurably worse, two reaches the plateau, and three and four buy nothing while costing 7
cascade samples per ray each. Halving it removes 14 of the 28 cascade samples every gather ray spent
in the hottest function of the dominant pass. The test now pins the constant in BOTH directions --
lowering it fails, and raising it "to be safe" also fails -- so the cost cannot creep back on a
hunch.

**What it actually saved, measured: 0.46 ms of 9.37, about 5% of the Resolve pass** (9.367 -> 8.909
with interpolation on; GI total 14.950 -> 14.438). Half the cascade samples in the hottest function
of the dominant pass bought 5%, which is far less than the sample ratio suggests and is the third
optimistic prediction in this work.

The reason is worth keeping: **sample count is not sample cost.** The resolve's 7 taps sit within a
voxel or two of each other and are highly coherent in the 3D texture cache; the trace's samples
march along a ray spanning tens of metres with poor locality. Counting samples treats those as
equal and they are not, by a large factor. So the remaining cost of the Resolve pass is the TRACE,
not the addressing -- which is where the next attempt should go, and it should be measured before
being sized. (Related: the interpolation's cost fell from 0.84 ms to 0.42 ms purely from the shorter
resolve loop, an occupancy effect rather than an additive one -- another reason not to reason about
shader cost by adding up operations.)

**Fixture note worth carrying forward.** The first version of that sweep showed no difference
between 1 and 4 iterations at all, because it only sampled flat box faces well inside cascade level
0 -- where the clipmap is very nearly an exact SDF and a single Newton step lands on the surface by
definition. It took a second box straddling the level 0 / level 1 CROSS-FADE BAND, where the sampled
field is a convex blend of two independently composed levels and therefore not a distance function,
before the counts separated. Same shape as the cascade continuity lesson: a fixture that only
exercises the easy region cannot distinguish a correct constant from a wasteful one.

### New finding: addressing agreement collapses inside the cross-fade band

**CONFIRMED, not yet fixed.** Bracket coverage is 87.0% for surfaces well inside a cascade level but
53.3% averaged over a fixture that is half in-band -- implying roughly **20% in the band itself**.
The band is the outermost `blend_voxels` (4) voxels of each level, so there is a shell at ~7-8 m from
the camera (and at each subsequent level boundary) where writer and reader largely fail to address
the same cell and the gather falls back to the environment probe.

The cause is structural and is the same one the cross-fade was introduced to fix for the FIELD: the
blended value is a mixture of two levels whose isosurfaces sit apart, so its zero level set moves as
the blend weight changes, and `resolve_surface_point` converges onto a moving target. Continuity of
the field was achieved; a single consistent *address* inside the band was not. Worth its own
investigation -- widening the interpolation bracket in the band, or resolving against a single level
rather than the blend, are both plausible and both need measuring.

Remaining highest value, in order: **`ray_count` 4 -> 2** (blunt, needs no code, should roughly halve
the dominant pass), the **cross-fade band addressing hole** above, then **A4** (skinned meshes, where
`asset_storage.h` claims a feature that does not exist), then **D1**.
