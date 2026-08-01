# USC-GI (Unravel Surface Cache GI) - status

Design: `tasks/surface_cache_gi_design.md`. Patterns learned: `tasks/lessons.md`.

## Done

- **Phase 1 - mesh SDF.** Sparse brick bake with filter borders, open-surface detection with
  unsigned fallback, welded-topology adjacency, winding check. Baked at import; runtime bake for
  procedural primitives. GPU atlas + indirection. Validated by `gi_tests` (4122 checks).
- **Phase 2 - global cascade.** Camera-centred clipmap, snapped origins for world stability,
  one level recomposed per frame. Instance set fingerprinted over placement AND identity, so a
  pure move forces a recompose.
- **Phase 0 - GPU lights.** All active lights resident, no frustum culling, traced shadows.
- **Phase 3 - radiance cache.** Open-addressed world hash. Key = cell + 6-way face + level, with
  a half-cell shift so surfaces do not sit on cell planes. Addressed in FIELD space
  (`SdfResolveSurfacePoint`) so writers and readers agree. Entries retire when the field no
  longer reports a surface at them.
- **Phase 3d - consumption.** `gi_resolve_pass` gathers the cache into an indirect diffuse buffer
  feeding the existing consumer, with a full denoise chain: count-driven temporal mean with
  Catmull-Rom history and luminance moments, variance-guided a-trous spatial filter boosted at
  low accumulation count, joint bilateral upsample.
- **Phase 4 - bounce.** Each entry casts a ray per frame: multi-bounce that grows with time
  rather than cost, and self-population of cells the camera has never seen. Entries store albedo
  and emissive and hold OUTGOING radiance, so colour bleeds and emitters contribute.
- **Phase 1b - bake cost.** A submesh's field is selected by that submesh's own face range, so
  the whole pass is linear in the model. It had been selected by DATA GROUP, which is the
  material index, so every submesh baked its entire material group -- wrong geometry as well as
  quadratic cost. The outer per-submesh loop is parallel with each individual bake serial; that
  pairing is mandatory, not a tuning choice (see lessons). Format version 14, so meshes recompile.

- **Phase 1c - bake budget.** A field is bounded by TOTAL voxels (`max_total_voxels`), not only
  by a per-axis resolution cap; the per-axis cap alone permitted 16.7M voxels in one field, which
  is both the reason Bistro overran the atlas 6.5x and the reason its bake took minutes, since
  both scale with voxel count. Enforced by coarsening the voxel, so coverage is never lost.
  Fields also bake from LOD 2 by default: a closest-point query costs roughly the SQUARE ROOT of
  the triangle count, not its logarithm (16x the triangles measured at 3.8x the time on an
  identical grid), so simplified geometry is a real saving and is independent of the voxel term.
  A LOD past the last generated one clamps to the coarsest available, never back to full detail.
  Format version 16.

- **Phase 2b - cascade continuity.** The finest covering level cross-fades into the next over a
  band at the edge of its coverage, rather than switching hard. Levels are composed independently
  at different voxel sizes, so their isosurfaces sit apart; measured, the two levels disagreed by
  up to 4.0 world units where a level-0 voxel is 0.25, and the hard switch stepped the sampled
  field by 64x the sampling step. Blended it is 1.7x. Stays conservative by construction (a convex
  combination of two under-estimates under-estimates) and the outermost level never fades, since
  beyond it lies only the give-up value. Debug mode 25 colours the answering cascade.

- **Phase 5 - instance culling.** A uniform WORLD-space grid (`sdf_instance_grid`) over the
  resident placements, walked with a DDA, so a ray tests the instances near it instead of all of
  them: measured 84x fewer bounds tests, with zero instances missed against a brute-force
  reference. Deliberately not a view-frustum froxel grid -- the radiance cache update pass casts
  from entries outside the frustum, and culling those against a frustum would drop the near-field
  tier for exactly the offscreen geometry the world-space cache exists to serve. Spans the union
  of all instance bounds rather than a camera box, so nothing ever falls out of the tier. The
  shader's traversal is pinned against the CPU reference by
  `test_instance_grid_shader_walk_matches_cpu`.

- **Phase 4b - material for bounce-discovered cells.** Each instance carries its submesh's albedo
  and emission, and a hit reports WHICH instance produced it, so a cell first discovered by a
  bounce ray is coloured instead of falling back to neutral grey. The mapping is already one to
  one -- a submesh is drawn with exactly one material and one field is baked per submesh -- so
  this needs no material voxels. Material is also filled in on an EXISTING entry that still has
  none, so a cell first reached through the cascade is coloured as soon as any near-field ray
  finds it. Debug mode 26 shows stored albedo, painting "no material" yellow.

- **Phase 2c - composition cost.** Instances are binned into a grid over each level, so a voxel
  tests the few that can reach it rather than every instance the level overlaps. Measured 9.7x at
  1600 instances (109 -> 11 ms for four levels) and byte-identical output, which
  `test_clipmap_culled_composition_matches_brute_force` pins. The remaining time is genuine field
  sampling, not culling: 2 voxels per cell performs the same as 4.

- **Phase 2d - recomposition policy.** Staleness is decided PER LEVEL, from a fingerprint of the
  instances reaching that level, so a move only invalidates the levels it actually touches and the
  caller no longer has to say "something changed" (it could only ever say "all of them"). The
  per-update budget now applies to instance changes as well as to origin drift, so one moving
  object costs one level per frame instead of four in a single frame. Order is by staleness AGE,
  which is what stops a fast camera -- which re-snaps the finest level almost every frame -- from
  starving the coarse ones forever.

- **Phase 2e - sampling cost.** `mesh_sdf::is_valid` walks the whole indirection array, which is a
  load-time integrity check; it was being called per SAMPLE, per instance in the composition cull,
  and -- worst -- for every level on every frame in the staleness fingerprint. With fields capped
  at 512 bricks that multiplied every field lookup by roughly 500x and cost ~3.3M iterations per
  frame before anything composed. All three now use the O(1) `is_sampleable`; the range check is
  kept, applied to the single entry actually dereferenced. Pinned by
  `test_sampling_cost_does_not_scale_with_field_size`.

  Measured on Bistro: one `Compose Level` 45 ms -> the WHOLE `GI/SurfaceCache/Update` 2.75 ms.
  Composition is no longer the cost of this system.

## Next

1. **Re-check the atlas after raising it.** `atlas_brick_dim` is 72 (373k bricks, 720 MB^3 texture
   at 373 MB), up from 64 (262k). Bistro was overrunning 262k; the warning now reports the running
   shortfall and the brick dimension that would hold it, instead of one line per refused mesh. If
   it still overruns, either raise it again (memory is CUBIC in this) or lower the importer's Max
   Total Voxels to make each field cheaper.
2. **Verify multi-submesh models.** Per-submesh SDFs are in (format version 16, so meshes
   recompile). Watch the atlas-full warning: a model like Bistro registers a field per submesh,
   which is many more fields than before, and the brick budget may need raising. Note that each
   field is now much SMALLER than it was, since it no longer carries its whole material group.
3. **Editor exposure.** Surface cache settings are compiled-in defaults. The pass settings structs
   need to live on the pipeline and be editable, so cell size, level cap, ray counts and denoise
   strength can be swept against the image. This is the component originally asked for; the
   leak-versus-cost trade in particular wants live tuning rather than chosen constants.
4. **Screen-space tier.** Depth buffer resolves the first metre exactly and cheaply, which is
   where sphere tracing is worst.
5. **Clipmap composition on the GPU.** Still CPU, and no longer urgent: the whole surface cache
   update is 2.75 ms on Bistro. Worth doing for its OTHER two reasons -- it removes the per-level
   upload, and it is what unblocks resolution 128 -- rather than for the frame time.


## Known limitations

- History is clamped to the current frame's 3x3 range rather than accepted or rejected, so very
  bright indirect light can be clipped toward its neighbourhood. `history_clamp_sigma` widens or
  disables it.
- Cells reachable ONLY through the global cascade still use `default_albedo`: the cascade is
  composed from many fields at once and cannot attribute a sample to one, so there is no material
  to read. Everything the per-instance tier reaches is coloured. Debug mode 26 paints the
  remainder yellow.
- Per-instance albedo is the material's BASE COLOUR FACTOR, not an average over its maps, so a
  texture-dominated material bounces its tint rather than its mean colour.
- Cell size is the dominant leak control: anything sharing a cell and a normal bin shares an
  entry. `max_level = 3` caps cells at 2 m.
- A moved instance is reflected in the cascade within `level_count / max_levels_per_update`
  updates rather than immediately, so it briefly keeps occluding from its old position. Bounded
  and eventually consistent by design -- the alternative was a four-level recompose in the frame
  anything moved. Raise `max_levels_per_update` to trade the hitch back for latency.
