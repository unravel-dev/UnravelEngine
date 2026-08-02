# Lessons

Patterns worth not relearning. Each entry is a mistake that actually happened, plus the rule
that prevents it.

## Keep work inside the workspace

Temporary harnesses, stub headers, and scratch builds belong in the repo, not in a system temp
directory. When a validation harness is worth writing at all it is worth keeping, so put it
under a `tests/` subdirectory excluded from the library glob and built as its own
`EXCLUDE_FROM_ALL` target (`engine/core/seq` and `engine/engine/rendering/gi/tests` both follow
this shape).

## A model's geometry is not drawn at the model's transform

`model_component` sits on a root entity, but the geometry hangs off child entities carrying
`submesh_component`, and `model::submit` uses each child's global transform DIRECTLY — it does
not compose it with the root (`batch_instance instance(transform_ptr)` in `model.cpp`).
Importers routinely bake an axis convention into that child node.

Anything that needs to place data in world space alongside the mesh (distance fields, physics
proxies, bounds) must read `model_component::get_submesh_transforms()` and fall back to the
root transform only when that list is empty — which is exactly what `model::submit` does.

Symptom when this is wrong: the data is rotated relative to the rendered mesh, while its
bounding box still looks plausible.

## Do not hardcode clip-space depth conventions

This project currently renders with a STANDARD depth range: 0 is near, 1 is far on D3D11. The
`INVERTED_DEPTH_RANGE` blocks in the shaders are latent support for a range that is not in use.
Reading those blocks and concluding the range is inverted is a mis-diagnosis that already
happened once here.

The rule is unchanged either way: never hardcode a clip z. Build clip coordinates with the
engine's helpers — `clipTransform`, `toClipSpaceDepth`, `clipToWorld` in `shaderlib.sh` — or,
for a camera ray, unproject one point at an arbitrary depth and take the direction from the
camera position. That is correct under both conventions, so it cannot break if the project ever
does switch.

More general lesson: a plausible mechanism that explains the symptom is not evidence. The
"flip" in question turned out to be the submesh transform above. Confirm the mechanism before
believing it.

## Dynamic GPU buffers do not grow through update()

`bgfx::update` past a dynamic buffer's allocated size is silently dropped; the shader then
reads zeros with no error anywhere. Track capacity explicitly and recreate the buffer when the
master copy outgrows it (`sdf_atlas::ensure_buffer_capacity`).

Zeros are especially nasty in numeric code: a zero divisor yields inf, arithmetic on it yields
NaN, and every NaN comparison is false — so validation branches fall through instead of
rejecting, and the result is confident-looking garbage rather than an error. Guard the divisor
explicitly (`if(!(voxel_size > 0.0))`).

## glm's SIMD path has no integer vector division

`ivec3 / int` fails to compile (there is no `_mm_div_epi32`), and `GLM_FORCE_DEFAULT_ALIGNED_GENTYPES`
is on, so integer vector types take the SIMD specialisation. Do grid/brick index arithmetic
with scalar ints.

Related: `math::inverse` on a `mat4` does not resolve. Namespace `math` declares its own
`inverse()` for `math::transform`, which hides the glm overloads from qualified lookup — call
`glm::inverse` explicitly.

## The shader include scan must follow #include

`asset_compiler`'s `needs_compute_buffers` detection selects the OpenGL profile. It originally
scanned only the `.sc` file, so a shader declaring its buffers in a shared `.sh` compiled at
GLSL 140 without SSBO support and broke on OpenGL alone — a backend-specific failure no other
platform reproduces. The scan now follows includes.

## Distance field invariants that are easy to get subtly wrong

These each cost a debugging round trip; all are now pinned by `gi_tests`.

- **BVH child indices.** With pre-order allocation the right child is NOT `left + 1` — the left
  subtree sits in between. Store the right child index explicitly.
- **Brick filter borders.** Resolve the brick ONCE per sample and filter inside that brick's
  bordered tile. Resolving per trilinear tap crosses into neighbouring bricks and reintroduces
  the seam discontinuity the border exists to prevent.
- **The field is a narrow band.** Voxels saturate at `encode_range`. Sphere tracing needs
  conservative under-estimates, not continuity, so saturation and surface/empty brick steps are
  by design. Assert conservativeness globally; assert accuracy and continuity only in-band.
- **Distance-to-bounds is degenerate on the bounds.** A ray entering a field starts exactly on
  its boundary, so reporting the bare distance-to-bounds there reads as an immediate hit and
  the tracer draws the bounding box, shaded by the box's own face normals. Add the padding the
  bake guarantees (`mesh_sdf::get_bounds_padding`).
- **Winding determines the sign.** A mesh authored inward-wound bakes an inside-out field, so
  it silently stops occluding. Detect it with the divergence-theorem volume and flip.
- **Open surfaces have no inside.** The pseudonormal test is exact only on a closed manifold;
  on an open mesh it reports "inside" for regions plainly outside, those bricks store a
  negative distance, and the tracer reads any negative sample as a hit — so the field's whole
  bounding box renders solid. Scanned props are frequently open, so detect it (count edges
  with fewer than two adjacent faces) and fall back to an unsigned shell. Bias toward
  unsigned: a nearly-closed mesh treated as a shell only loses interior solidity, while an
  open mesh treated as closed produces phantom geometry.

## Compute mesh adjacency on WELDED topology, never on vertex indices

Exporters split vertices at UV and normal seams, so two triangles meeting along a seam
reference different indices for the same point. Index-keyed adjacency therefore reports a
closed mesh as riddled with boundary edges, and gives every seam vertex only the faces from one
side — which tilts its pseudonormal and flips the sign of the voxels near it.

Weld by quantised position first, then build edge and vertex adjacency on the welded indices.

Weld with an **exact** key, not a hash. A first attempt keyed the weld map by a mixed hash of
the quantised coordinates, so any hash collision welded two unrelated vertices; that corrupted
the pseudonormals at both and took the number of wrongly signed samples from 3 to 1229. Use the
quantised integer triple as the key and let the hash serve only as a bucket.

## A compiled asset is a function of the CODE that produced it

Changing `mesh_sdf_baker` without bumping `get_format_version<mesh>()` leaves every already
compiled mesh carrying the field the OLD code produced. Verifying a baker fix in the editor
then measures stale data and the fix looks ineffective. Bump the format version whenever the
bake output changes, not only when the serialized layout changes.

## Keep a value's transformations in ONE place

The shell thickness of a two-sided field was subtracted twice: once by the bake when writing
voxels, once again by the shader when sampling. The result was a field shifted inward by a
whole thickness, so samples just inside the bounds went negative and the tracer drew the
bounding box — dithering in and out as the ray direction crossed the sign boundary.

Two things let it hide. It is invisible on signed fields, where the thickness is zero, so a
test suite that only covers closed meshes agrees perfectly. And the CPU reference did NOT
subtract, so CPU and GPU disagreed only for two-sided fields. Whenever a quantity can be
applied at bake time or at sample time, apply it in exactly one and say so at both sites — and
make sure the parity test covers a case where the quantity is non-zero.

## Thresholds must be expressed in the units of the thing they judge

The distance-field tracer used an absolute world distance as its "close enough to be a hit"
threshold. A field resolves nothing finer than one voxel, and a voxel's world size depends on
both bake resolution and instance scale — so an absolute threshold larger than an instance's
voxel makes even the saturated far-field value read as a hit, and the field's bounding box
renders solid. It is expressed as a fraction of a voxel now.

This misbehaves in a way that actively misleads: it comes and goes with instance scale and
camera distance, so it looks like a data or transform problem rather than a units problem.

## Anything scaled to "a voxel" must name WHICH voxel

A cascade's levels differ in voxel size by orders of magnitude. The clipmap tracer took its hit
threshold and its central-difference gradient epsilon from the FINEST level regardless of which
level answered, so wherever a coarse level responded it was differencing across a fraction of
one voxel — a region where the field is effectively flat. The normal collapsed into
quantisation noise and the traced surface broke into bands.

The sampler now reports the voxel size of the level that answered, and everything voxel-scaled
derives from that. Generally: when a multi-resolution structure returns a value, it must return
the resolution it answered at, or every caller silently assumes the wrong one.

## Cone tracing grows the ACCEPTANCE radius, not the step

Sphere tracing converges slowly along a ray grazing a surface, because every step is limited by
a distance that stays small the whole way. The cure is to treat the ray as a cone whose radius
grows with distance — but the growth must go into what counts as a HIT, not into a forced
minimum step.

Forcing a minimum step larger than the distance to the surface makes the ray jump straight
through it. A grazing ray then punches through a floor and misses in bands, which renders as
concentric rings and is far worse than the fade it was meant to cure. Widening the acceptance
instead can only ever stop a ray EARLY, never past a surface, so the trace stays conservative
and still terminates in O(log D) steps because each step advances by at least the current
radius.

The visible symptom of the wrong version — banding or rings rather than a clean surface — is
the signature of a march stepping over geometry.

## Compute shaders cannot sample with implicit derivatives

`texture2D` and anything built on it (`DecodeGBufferNormalMetalRoughness` and friends) needs
screen-space derivatives, which a compute shader has no way to produce. D3D rejects it with
`error X4532: cannot map expression to cs_5_0 instruction set`, which does not name the
sampling call at all. GLSL 430 accepts it silently, so this is another backend-specific trap.

Use the explicit-LOD variants (`...Lod`, `texture2DLod`) in every compute shader.

## HLSL rejects ternaries whose arms it cannot unify

Chained ternaries and ternaries returning a struct compile fine as GLSL/SPIR-V and fail on the
D3D backend with `error X3020: type mismatch between conditional values`. Use explicit
if/else in shared shader code — the failure is backend-specific, so it will not show up until
someone builds for D3D.

More generally: compile every shader for `s_5_0`, `spirv`, AND `430` before calling it done.
Each backend rejects a different subset of otherwise valid code.

## Debug views should isolate stages, not just show the result

Every failure in the field residency path — indirection unbound, atlas unwritten, wrong slot
arithmetic — degrades into per-pixel noise that looks identical whichever stage broke. A probe
mode that reports the intermediate lookup (`SdfProbeLocal`, debug mode 18) settles in one frame
what a screenshot of the final image cannot settle at all.

More generally: when a GPU result is wrong, mirror the arithmetic on the CPU and diff it
against the reference implementation before touching the shader
(`test_gpu_addressing_matches_cpu`). It runs in milliseconds and either finds the bug or
definitively clears that stage.

## Compute dispatches still need the view set up

bgfx supplies `u_view`, `u_proj`, `u_invViewProj` and friends PER VIEW, and a compute dispatch
runs on a view like anything else. A dispatch that reconstructs world positions from depth
therefore needs `set_view_proj` exactly as a draw would; without it the matrix is whatever that
view id last held, and every reconstructed position is silently wrong.

This cost a debugging round trip on the radiance cache: the insert dispatch built its keys from
garbage world positions, so the lookup -- whose positions were correct -- could never match one.
There is no error and no NaN, just a cache that reads as permanently empty, which looks like a
key derivation bug rather than a missing matrix.

Rule: if a compute shader uses ANY of bgfx's built-in view uniforms, set the view state on its
pass. Grepping the shader for `u_view`/`u_proj`/`u_invViewProj` is the check.

## Distinguish "empty" from "mismatched" before theorising

A hash lookup that misses looks the same whether nothing was ever written or the reader derives
a different key than the writer, and no amount of moving the camera separates them. The
decisive view ignores the key entirely and shows raw STORAGE -- occupied slots, and whether
their payload is non-zero (`SDF_DEBUG_CACHE_SLOTS`, debug mode 23). One frame of that splits the
hypothesis space in half.

This is the same lesson as the SDF probe view above, and it has now paid twice: for any indexed
structure, build the "show me what is actually in there" view before reasoning about why a
lookup fails.

## A traced hit is a distance, not a position

A sphere trace stops when the field falls under its acceptance radius, so the hit is short of
the surface ALONG THE RAY. Head on that is a small depth error and harmless. At grazing
incidence the same error is a large displacement ACROSS the surface, so a hit and the G-buffer
position of the identical surface address different cells, and the error grows with view angle.

The signature is a miss pattern correlated with surface ORIENTATION rather than scattered
randomly: a floor that misses everywhere while walls viewed head on hit. Scattered misses mean
capacity or hashing; oriented misses mean geometry.

Project onto the isosurface before using a hit as a coordinate (`SdfProjectToSurface`). One
Newton step along the gradient is enough, because the field is a signed distance.

## An open addressed table near saturation fails as BOTH writer and reader

At high load a short probe chain drops most inserts and misses most lookups, so the table reads
as densely populated and simultaneously never hit -- which looks like a key derivation bug and
is not one. Size for the real demand and check occupancy before theorising about hashing.

For a distance-graded world cache the demand is dominated by the FAR field, not by detail near
the camera: cell size grows linearly with distance but the area covered grows quadratically, so
the coarse levels consume most of the table. A ground plane out to the insertion range alone
runs to tens of thousands of cells.

Corollary for the diagnostic view: derive its grid from the capacity. A fixed grid over a larger
table shows a fraction of it and reads as headroom that does not exist.

## Quantisation boundaries must not pass through the COMMON case

Two degeneracies in the radiance cache key, both of which made large flat surfaces miss forever
while edges and thin features hit -- a miss pattern correlated with surface orientation.

- **Normal binning.** Subdividing each cube face 2x2 puts a bin boundary exactly through the face
  CENTRE, i.e. through the axis-aligned directions. A floor normal of (0, 1, 0) then has its bin
  decided by the sign of two components that are both zero. The writer reads its normal from the
  G-buffer and the reader from the field gradient, so they differ by an epsilon of the wrong sign
  about half the time. Six faces put the axis directions at bin centres instead.
- **Cell snapping.** `floor(position / cell)` puts a grid plane at y = 0, which is exactly where a
  ground plane sits. Shift by half a cell along the quantised face direction first, so the
  surface lands in a cell interior. Derive the shift from the QUANTISED face, never the raw
  normal, or the two sides shift differently and the fix reintroduces the problem.

General rule: pick the quantisation so the most common inputs land at bin CENTRES. Boundaries are
where independent producers of the same value disagree, so a boundary through the common case
fails constantly while looking like a hashing or capacity bug.

Where two sources genuinely cannot agree, make the reader probe the runner-up bin as well
(`find_surface`). Near a tie both sides agree on the SET of the top two even when they disagree
on the order, so probing both always covers the writer's choice.

Test note: the original test perturbed the normal in ONE direction only and passed. A degeneracy
test has to sweep every sign combination, and should assert that the case it exercises really is
degenerate -- otherwise it silently stops testing anything when the code changes.

## A cache of the FIELD must be addressed in field space

The radiance cache was written from the rasterised G-buffer and read from SDF ray hits. Those are
two different surfaces: the field's zero level set is displaced from the rendered triangles by a
fraction of a voxel, and a sphere trace stops short of even that by its acceptance radius. The
disagreement is roughly a voxel, which is the same order as a cache cell -- so writer and reader
addressed different cells and never saw each other's work.

The tell is that the miss rate tracked how well the field approximates the geometry: large flat
surfaces missed almost entirely, small detailed props hit, because a fine per-instance field
answers for the props while the coarse clipmap answers for the walls. Not a hash problem, not a
capacity problem, and no amount of tuning either one moves it.

Fix the ADDRESS, not the tolerance. Both sides now converge onto the field's own isosurface and
take the normal from the field's gradient (`SdfResolveSurfacePoint`) before deriving a key, so
they are quoting one function instead of two approximations of it. Widening the cell or probing
neighbours only reduces the miss rate in proportion to error/cell_size and never removes it,
because the error is tangential as well as normal.

Corollary: several Newton iterations, not one. A single step lands somewhere that still depends
on where it started, and the two callers start a voxel apart.

Corollary: the recorded position must UNDO the key's half-cell lift. The lift is an addressing
device to keep surfaces off cell boundaries; leaving it in the stored payload would light a point
floating half a cell above the surface.

## Parameters two passes must agree on need ONE owner

The radiance cache key is derived from cell size, base distance and level cap. Those were
duplicated in each pass's settings struct, with a comment warning they had to match. A comment is
not a mechanism: the moment a third reader appeared, the duplicate was one edit away from a
silent total failure, because a writer and reader that disagree derive different keys from the
same surface and simply never find each other -- which reads as an empty cache, not an error.

They now live on `radiance_cache_gpu` and every pass reads them from there. Generally: when
correctness depends on two sites holding identical values, give the value one owner rather than
documenting the coupling.

## A pass that selects by PRESENCE must clear its output when it does not run

The indirect consumer picks the surface cache buffer over SSIL by whether the texture exists. A
buffer left in the render view from when the pass last ran therefore keeps overriding SSIL with a
frozen image forever -- which looks like GI that stopped updating rather than like a disabled
feature, and survives toggling the thing that was supposed to control it.

Any early-out path has to remove the output (`tex_remove` / `fbo_remove`), not merely skip the
work. The existing SSIL pass already did this; it is easy to miss when adding a second producer.

## A shared history resource needs a condition covering EVERY consumer

`PREV_DEPTH` is snapshotted only when the Hi-Z stack is active, because that stack was its only
consumer. Adding temporal accumulation to the GI resolve made it a second consumer, and since it
treats a missing previous depth as "no history" -- the right default, since there is no neutral
history to blend -- GI convergence silently became conditional on an unrelated feature being
enabled. The symptom is not an error but noise that never settles no matter how long the camera
holds still.

When adding a consumer of a shared history buffer, extend the condition that PRODUCES it. Any
"is this feature on" guard around a resource is a guard around every future user of it.

Diagnostic note worth reusing: the noise MOVING each frame was itself the evidence that ruled out
the ping-pong and the frame counter. The gather seeds its sample pattern from the frame index, so
visibly changing noise proves the index advances and the read/write targets alternate. Prefer a
symptom that discriminates over one that merely confirms.

## math::transform is not a mat4 -- always call get_matrix()

`gfx::set_uniform(u, &some_transform)` compiles cleanly and uploads whatever occupies the first
64 bytes of the object, which is not the matrix. Pass `transform.get_matrix()`, as every existing
call site does.

This is nearly invisible in a temporal pass: garbage reprojection sends every pixel somewhere
invalid, the history test rejects it, and the pass degrades exactly into the disabled behaviour it
is supposed to fall back on. The result is noise that never converges -- indistinguishable from
the feature not running, which is what it cost here.

Rule with teeth: when a uniform takes a matrix and the value is any wrapper type, copy the idiom
from an existing working call site rather than taking an address. `&object` compiling is not
evidence that `&object` is the right pointer.

## A pass that disables itself must not DESTROY a shared resource

`run_hiz_pass` released `PREV_DEPTH` whenever Hi-Z was not wanted, which was correct while Hi-Z
was its only consumer. Once the GI resolve started validating reprojected history against the
same texture, that release ran every frame at the top of the pipeline and deleted the texture the
GI pass -- further down the same frame -- was about to read. The end-of-frame snapshot dutifully
recreated it, the next frame deleted it again, and accumulation never once had a history.

Producing a resource conditionally is not enough; its DESTRUCTION has to be conditional on the
same set of consumers. Lifetime belongs to the one place that decides whether to produce it, not
to whichever consumer happens to run first and notice it does not personally need it.

Symptom shape worth recognising: the feature works only when an unrelated feature is enabled.
That was the observation that cracked this one, after four wrong hypotheses -- and the log line
that named WHICH input was missing turned a guessing game into a single lookup. When a stage can
silently degrade, make it say so, with the specific missing input named. Cheaper than any number
of screenshots.

## A fixed temporal blend weight never converges

`mix(current, history, 0.92)` is an exponential moving average, and an EMA converges to a
DISTRIBUTION, not to a value: its steady-state deviation is the per-frame noise times
sqrt((1-a)/(1+a)), about 20% at 0.92. That residual never decays, so the image keeps shimmering
however long the camera is held still, and raising the constant only lowers the floor while making
disocclusion recover more slowly.

Accumulate a sample COUNT and blend by 1/n while n grows to a cap. That is a true running mean --
early frames converge fast AND the estimate actually settles -- and the cap is what preserves
responsiveness to lighting that really changed.

Two companions matter as much for the "still crawling" symptom:

- **Resample history with Catmull-Rom, not bilinear.** Reprojection lands between texels every
  frame, so history is resampled every frame; repeated bilinear taps compound into unbounded
  softening and make residual noise crawl across surfaces.
- **Carry luminance moments** so the spatial filter can size its edge stop from measured variance.
  Where the estimate has settled the tolerance collapses and the filter stops touching the pixel,
  which preserves detail AND removes the filter itself as a shimmer source. A fixed tolerance can
  only trade one against the other.

## HLSL fragment outputs are main's parameters, not globals

Writing `gl_FragData[N]` from a helper function compiles on GLSL/SPIR-V and fails on D3D with
`error X3004: undeclared identifier bgfx_FragData0` -- an error naming a generated symbol rather
than the output. Compute the values in helpers, return them through out parameters, and assign
`gl_FragData` only inside `main` (the existing SSIL resolve already follows this shape).

## Reduced-resolution effects need a SURFACE-AWARE upsample

Sampling a half-resolution buffer with a plain bilinear tap is wrong at every silhouette: the
low-resolution texel straddling the edge holds a blend of foreground and background, and bilinear
interpolation spreads that blend to both sides. For indirect lighting this is worse than a soft
halo, because the gather is noisiest exactly at edges -- reprojection fails there and the spatial
filter rejects most of its taps -- so the fringe reads as bright speckles rather than as blur.

Weight the four candidates by whether they belong to the same surface as the pixel being shaded.
The guide can be EXACT rather than approximate here: the gather sampled the G-buffer at the
low-resolution texel centres, so re-sampling the full-resolution G-buffer at those same centres
recovers precisely the surface each texel was computed for.

Always keep a fallback for the all-rejected case (a thin feature whose neighbours all belong to
other surfaces). Falling back to the plain bilinear tap is strictly no worse than not having the
upsample at all, whereas returning zero punches a hole.

## Whoever CLAIMS a slot owns initialising its whole payload

The radiance cache clears only its keys at startup, on the reasoning that an entry is unreachable
until its key matches -- which is sound, and which makes claiming a key exactly the moment it
stops being true. So the claim itself has to initialise the payload.

That was fine while every claim came from one place that immediately wrote every field. Adding a
second claim site (bounce rays creating entries) and two new fields (albedo, emissive) broke it
from both directions: the new site wrote only position and normal, and the eviction path reset
only radiance. Uninitialised memory then read back as EMISSIVE, which is light no surface in the
scene emits -- arbitrary saturated blobs wherever a slot happened to be recycled.

The fix belongs in `GiCacheInsert`, not in its callers: it is the only code that can distinguish a
fresh claim from a repeat, since callers get a slot either way and zeroing on every call would
reset the accumulation every frame.

General rule: when a structure skips a bulk clear for a good reason, the invariant that replaces
it ("claiming initialises everything") must live in the claim function, and adding a field to the
payload means revisiting every claim path. A field that is merely *usually* written is a field
that will eventually be read uninitialised.

## A dropped build dependency is a STALE BINARY, not a build error

`resolve_dependencies<gfx::shader>` resolved `#include` only against the including file's own
directory. Shaderc also searches the shared shader root, so every root-relative include
(`#include "gi/radiance_cache.sh"` from inside `shaders/gi/`) resolved to a path that does not
exist and was silently dropped from the manifest.

Nothing fails at build time. Editing the shared header recompiles only the `.sc` files that were
themselves edited, and every other shader keeps running code generated from the PREVIOUS version
of the header. Changing `GI_CACHE_DATA_STRIDE` from 3 to 5 therefore left writers indexing at
stride 5 and readers still at stride 3: each read landed in a neighbouring entry's fields, and
position and normal data interpreted as radiance rendered as saturated primary-coloured cells.

Two things to carry forward:

- Saturated, "impossible" colours -- pure red, green, blue, white -- are the signature of reading
  a DIFFERENT FIELD, not of arithmetic going wrong. True numerical garbage is arbitrary, often
  huge or NaN. A small palette of clean primaries means a layout disagreement.
- Compare source and compiled TIMESTAMPS before theorising about shader logic. One `ls` settled
  in seconds what several rounds of reading the code did not, and it is worth doing whenever a
  shader behaves as though an edit did not land.

This is the second time include resolution has been the culprit here: the same omission in
`needs_compute_buffers` picked the wrong GLSL profile earlier. When a build step interprets
`#include`, it must search every path the real compiler searches.

## A radiance cache that gathers from itself must exclude its OWN cell

Entries feed on each other by design -- that is what makes the bounce count grow with time rather
than with cost. The loop is convergent only because each step is scaled by an albedo below one:
equilibrium sits at 1/(1-albedo) times the single-bounce value.

A ray that resolves back to the cell it started from breaks that. The entry becomes a term in its
own sum, L = albedo * L + rest, which settles far too bright and, with a near-white albedo, keeps
visibly climbing long after the genuine bounces have settled. Grazing rays hit this case readily,
because the trace starts a small bias off the surface and accepts a hit within a fraction of a
voxel. Compare the resolved SLOT, not the position.

Worth separating two things that look alike here: a slow brightening ramp is EXPECTED from
progressive multi-bounce and is energy-correct, since each frame adds roughly another bounce and
white walls legitimately multiply the single-bounce answer several times over. Self-feedback looks
the same at a glance. The distinguishing question is whether it settles.

Note the receiver side does NOT need this exclusion: the gather writes to the screen and never
back into the cache, so no loop closes there, and excluding the shading point's own cell would
discard legitimate near-field bounce from the same wall.

## Cell size is the dominant control on light leaking in a hash cache

Everything inside one cell that shares a normal bin shares ONE entry, so two parallel walls closer
together than a cell are served each other's light. That is leaking by construction of the key --
no amount of tracing accuracy recovers it, because the merge happens before any ray is cast.

The level cap is the setting that matters, and it is easy to set far too generously: at level 6 a
0.25 m base cell reaches 16 m across, larger than most rooms. The same coarseness also averages
small emitters away to nothing, so "the emissive object stops contributing at distance" and "light
leaks through walls" are frequently ONE symptom with one cause.

Prefer keeping cells fine out to a useful distance and paying in entries. Table capacity is cheap
and measurable (the slots view); a merged key is neither observable nor recoverable.

## World-anchored data needs an explicit retirement rule

A cache anchored to the world outlives visibility on purpose -- that is the whole reason for it.
The flip side is that nothing reclaims an entry when its surface MOVES or is deleted: it keeps its
stored material and goes on radiating at a position where there is no longer anything. Eviction
does not save it either, since that only fires on probe-chain contention, which a lightly loaded
table never produces. A moved emitter therefore leaves a permanent imprint of itself behind.

The field already knows the answer: an entry sits ON a surface, so the distance sampled there
should be about zero, and open space means the geometry has gone. Validate and free. Treat a
"no coverage" reading as unknown rather than as gone, or the entire far field gets deleted.

Two-part failure worth noting: the validation only works if the FIELD itself notices. The cascade
was recomposed when the instance COUNT changed, a proxy that misses a pure move entirely -- the
object leaves its geometry behind, keeps occluding and lighting from where it used to be, and
nothing downstream can recover, because everything else asks the field what is there. Fingerprint
over placement AND identity, order-independently.

General shape: any "persist until something invalidates it" design needs the invalidation rule
written at the same time as the persistence, and needs the thing it consults to be itself
up to date.

## One field per SUBMESH, because submeshes are drawn at their own transforms

`model::submit` draws each submesh at its child entity's global transform, and importers
routinely differ those between submeshes of one model. A single field baked over the whole mesh
therefore cannot be placed correctly: it would have to be at several transforms at once.

The original code placed the whole-mesh field once per transform in the pool, which duplicates
the ENTIRE model once per transform. On every test asset -- one submesh, one transform -- that is
exactly right, so it looked correct throughout development and only failed on a real imported
model, where it fills the scene with phantom copies that occlude and bounce light.

Same shape as the hardcoded cache parameters that happened to equal the defaults: a bug that is
invisible in the simple case and total in the real one. When indexing collapses to a single
element in every test, the indexing itself is untested.

Registration must also honour the submesh-to-transform MAPPING and the per-instance active flag,
rather than iterating the shared transform pool: the pool is deduplicated, so its order carries no
relationship to submeshes at all.

## Per-part work must be sized to the PART, not to the whole

Splitting the SDF bake per submesh, the shared vertex buffer was passed through whole and only the
index list filtered -- justified in a comment as costing only memory "the baker does not care
about". Wrong axis: every per-vertex pass in the baker (bounds, position welding, vertex
adjacency) is linear in the vertex COUNT it is handed. Handing it the entire model once per
submesh makes baking QUADRATIC in model complexity. A few thousand submeshes over a few million
shared vertices turned a seconds-long bake into one still running after five minutes, while each
submesh individually is tiny.

Compact and remap. And when justifying a shortcut in a comment, name the axis being traded --
writing "only costs memory" is what stopped the time cost being noticed.

Related: the symptom "fast on every simple mesh, apparently hung on the real one" is the signature
of super-linear complexity, not of a deadlock. Estimate the exponent before hunting for a hang.

## An identifier that is 1:1 in every test asset and N:1 in real content

The per-submesh SDF selected its triangles by `data_group_id`. That reads like a submesh id and is
not one: it is the MATERIAL index, and a model has far more submeshes than materials. Every submesh
therefore baked its whole material group -- its siblings' geometry, at its own transform -- and the
pass cost submeshes x group_size instead of submeshes x own_size. On Bistro that is the whole
difference between seconds and minutes, and it also silently reintroduced the phantom-copy bug that
splitting the field per submesh existed to fix.

It survived review because on every test asset there is one submesh and one material, so the two
keys are indistinguishable. That is now the third bug here with exactly this shape (hardcoded cache
parameters that happened to equal the defaults; a whole-mesh field placed once per transform), and
the shared tell is worth stating on its own:

**When a mapping collapses to the identity in all your fixtures, the mapping is untested.** Write
the fixture where the two sides differ -- more submeshes than materials, more transforms than
submeshes -- and assert the cardinality directly. `test_submesh_extraction_selects_only_its_own_submesh`
does this by checking that the whole pass extracts each model triangle exactly ONCE; stated as a
count it is both the correctness property and the complexity property, and unlike a timing
threshold it holds on any machine.

Corollary for selection by range: `submesh.face_start` / `face_count` is a contiguous range into
`load_data.triangle_data`, preserved by both the skinning and the LOD passes. Selecting by it is
what makes the pass linear; scanning all triangles and filtering costs submeshes x triangles even
when the filter is correct. The same applies to the remap buffer -- an array indexed by source
vertex id costs one allocate-and-clear of the whole model per submesh, which is the identical
quadratic term measured in memory traffic instead of in triangles.

## "Is the container populated" is not "is MY entry populated"

The surface cache decided whether a model's submeshes had their own node transforms by testing
`!submesh_to_transform_indices.empty()` -- the OUTER list. That list is sized to the submesh count
up front (`submesh_pose_mat4::reserve` resizes rather than reserving), so it is non-empty for every
model including ones where nothing was ever mapped. A primitive has no child entity carrying a
`submesh_component`, so its pose is sized and never mapped: the check read as "the hierarchy
resolved", the per-submesh loop iterated an empty inner list and placed nothing, and the `return`
after it skipped the root-transform fallback entirely. Primitives vanished from GI while still
rendering perfectly, which reads as a bake or residency failure rather than a placement one.

Two rules out of it:

- Ask the question about the ELEMENT, not the container. `has_transforms(submesh_index)` was
  already there and already correct; the outer-emptiness test was a paraphrase that happens to
  agree only when the container is empty overall.
- A fallback reached by falling out of a loop is not a fallback. If the loop body can legitimately
  do nothing, the `return` after it swallows the case the fallback exists for. Make the choice
  explicit per item -- branch, then handle both sides -- rather than letting one side be "whatever
  is left after the loop".

The deeper fix is that this decision already had an owner: `model::submit` makes exactly the same
choice per submesh (`has_transforms` -> mapped transforms, else the model's own transform). Any
pass that places data alongside rendered geometry -- distance fields, physics proxies, bounds --
must reproduce that branch rather than invent a paraphrase of it, because the two disagreeing is
invisible until some asset shape exercises the difference. Same lesson as the whole-mesh field
placed once per transform, and it is now the second bug in this family found in one sitting.

## Look for the mapping that already exists before adding a parallel structure

The plan for colouring bounce-discovered cells said "needs material voxels" -- a second volume
alongside the distance field, storing albedo per voxel. That would have been several times the
atlas memory and a whole new bake output.

It was unnecessary, because a one-to-one mapping was already in place: a submesh is drawn with
exactly one material, and since the per-submesh split there is exactly one field per submesh. So
the INSTANCE is a perfectly good place to hang the material, and the only missing piece was that a
ray hit did not report which instance produced it. Two floats-worth of instance data and one extra
field on the hit struct replaced a parallel voxel volume.

The general move: before adding storage, look for something that is already keyed the way you need.
The per-submesh field split had been done for an unrelated reason (transforms), and it silently
made per-material attribution exact.

What this cannot do is worth stating with it: the global cascade composes many fields into one
volume, so a cascade sample genuinely cannot be attributed to a single instance, and those cells
keep the neutral fallback. A view that paints the un-attributed cells (debug mode 26 paints them
yellow) is what keeps that honest limitation visible instead of it reading as a bug later.

## Cull in the space the RAYS live in, not the space the camera lives in

The plan called the instance broad phase a "froxel cull grid", borrowing the name from clustered
lighting, where the grid subdivides the camera frustum. That would have been the wrong structure.
Rays into the per-instance tier come from four callers, and only one of them is a camera ray: the
radiance cache update pass casts from cache ENTRIES, which are world-anchored and deliberately
outside the frustum -- that is the entire reason the cache is world-space. A frustum-shaped grid
would have culled those rays against a region they do not occupy, silently removing the near-field
tier from exactly the offscreen geometry the design exists to serve.

Before building an acceleration structure, enumerate who actually queries it. The name a task
inherits from another domain carries that domain's assumptions with it.

Related choice, same reasoning: the grid spans the union of all instance bounds rather than a
camera-centred box. A camera box is cheaper to size, but anything beyond it loses the exact tier,
and that does not present as a performance decision -- it presents as thin geometry that stops
occluding once you walk away from it.

## A broad phase may over-report, never under-report

A culling structure has one asymmetric invariant: a false positive costs one rejected bounds test,
a false negative is geometry a ray never tests and therefore light leaking through a wall. Every
design decision should be resolved in that direction:

- An instance is listed in every cell its bounds touch, so a traversal can encounter it more than
  once. Guaranteeing exactly-once means deciding, at a cell boundary, whether a ray's entry into an
  instance belongs to this cell or the next -- a floating-point tie that can drop the instance from
  both. The duplicate is nearly free (the broad phase is already capped by the nearest hit so far,
  so a repeat behind a hit rejects immediately); the miss is not.
- When the cell budget is exceeded, COARSEN the cells rather than cropping the grid. Cropping
  leaves instances unreachable, which is the same failure by another route.
- Keep the ungridded path as the fallback when no grid is resident. Testing everything is slow, not
  wrong; skipping the tier is wrong.

Test it against the brute-force version it replaces, over the awkward cases specifically: rays
axis-aligned along cell planes, and origins outside the grid. Assert both that nothing is missed
and that the work actually dropped, or the test cannot tell a correct structure from a useless one.

## A second implementation of an algorithm needs a parity test, however obvious it looks

The grid walk exists twice: once on the CPU, once in the shader, because GLSL cannot be handed a
C++ class. The two are the same algorithm but not the same code -- the CPU branches per axis, the
shader uses vector masks and `step()` to handle ties without branching. "Obviously equivalent" is
precisely the claim worth testing, and the failure mode is silent: an instance the GPU never
visits looks like a field that stopped occluding, not like a traversal bug.

Transcribing the SHADER's formulation back into the test, and diffing its reachable set against the
CPU reference over thousands of rays, costs a few milliseconds and covers the cases hand-reasoning
skips. This is the third time that shape has paid here, after the GPU addressing and the cache key
transcription tests.

## Sub-pixel jitter defeats any binary history test

The GI temporal pass validated history with two accept-or-reject tests, and it could not be tuned:
strict gave fireflies, permissive gave ghosting. The fireflies appeared with the camera completely
STILL, which is the observation that cracked it -- nothing in the scene was moving, so nothing
should have been rejected.

TAA jitter was moving it. The projection is offset by a fraction of a pixel every frame, so the
reprojected sample lands somewhere slightly different each time, and the previous depth buffer was
rasterised at a different sub-pixel offset again. On high-frequency geometry -- foliage, railings,
ivy -- a strict test fails constantly under that, and every failure drops the pixel to a single
frame of a four-ray gather. A pixel showing one frame of that IS a firefly.

The normal test was worse than merely strict: it sampled the CURRENT normal buffer at both `uv` and
`prev_uv`, so it never compared the previous surface to this one, only two points of this one a
jitter apart.

The fix is not a better threshold, it is to stop making the choice binary. Clamping history to the
current frame's local colour range (mean +/- k sigma over a 3x3) bounds both failures at once:
history that agrees survives intact, history that disagrees is pulled to the edge of what this
frame sees. Outliers cannot persist and stale values cannot stray. A reprojection test is still
useful as a coarse guard for real disocclusion, but it no longer has to be exact.

General shape worth recognising: when a parameter has no good value because both directions produce
a different artefact, the mechanism usually needs to become continuous rather than the threshold
needing to move.

## A validation helper called per sample is a hidden multiplier on everything

`sample_mesh_sdf` guarded itself with `mesh_sdf::is_valid()`. That function walks the WHOLE
indirection array to prove every surface entry indexes storage that exists -- a load and
upload-time integrity check, correct where it was designed to be used, and linear in the field's
brick count. Called per sample it multiplied every lookup by the number of bricks in the field:
with the voxel budget capping fields at 512 bricks, roughly 500x.

What made it survive three rounds of optimisation is that it never looked like a slow function.
The visible symptom was the clipmap composition being expensive, and every number around it read
healthy -- the instance cull was working, candidates per voxel had dropped 71x, the pool threads
were saturated. The work that remained was simply hundreds of times more expensive per unit than
anything suggested. Two structural fixes aimed at REMOVING work each landed a fraction of their
predicted gain, for the same reason: they reduced the count of an operation whose unit cost was
the actual defect.

Rules:

- A helper named `is_valid` / `validate` / `check` is written for the setup path. Before calling
  one in a loop, read it. Linear scans hide comfortably inside a name that sounds like a
  predicate.
- Keep the guarantee, move the check. The out-of-range protection did not have to be dropped:
  range-checking the ONE entry actually dereferenced is the same guarantee for O(1) instead of
  O(bricks). "Expensive precondition" is usually "precondition asserted at the wrong granularity".
- When a fix that provably removes most of the work does not move the wall clock, stop optimising
  the count and go measure the unit. Two consecutive under-delivering fixes is the signal.

Guarded now by `test_sampling_cost_does_not_scale_with_field_size`, which asserts that 19x the
bricks does not cost more per sample -- a property, unlike a timing threshold, that holds on any
machine.

## An invalidation flag that can only say "everything" will say it constantly

The cascade was told to rebuild by a single bool derived from a fingerprint of the whole instance
set. That is correct -- a pure move has to invalidate the cascade or the object goes on occluding
from where it used to be -- but it carries no information about WHERE, so the only safe response
was to recompose all four levels, unbudgeted, in the frame anything moved. Any animation in the
scene therefore paid the full cost every frame.

The fix was to move the question to where the answer lives. The cascade knows each level's bounds,
so it can fingerprint the instances reaching each level separately and decide staleness per level.
The caller stops passing a flag it cannot make precise.

Two things that make the budgeted version safe rather than merely faster:

- **Bounded staleness is a different thing from unbounded staleness.** Deferring a level by a few
  frames leaves geometry briefly out of date; never noticing a move at all leaves it wrong forever.
  Only the second is a correctness failure, and it is worth being explicit about which one a
  throttle is trading for.
- **Order by AGE, not by index.** The finest level has the smallest voxels and so re-snaps most
  often; a finest-first policy lets a moving camera keep it permanently at the front of the queue
  and the coarse levels never rebuild. That failure reads as "the cascade does not work at
  distance", which is nothing like the scheduling bug it actually is.

## A blocked thread reads as idle, so parallel work is invisible where it is submitted

`GI/Clipmap/Compose Level` profiled as 45 ms wall with **746 us busy (2%)** on the main thread.
That looks like a scope doing almost nothing. It was the opposite: `std::for_each(poolstl::par, ...)`
submits chunks and then BLOCKS the caller on futures, so the enclosing scope measures how long the
main thread waited, and the work is on pool threads the scope does not cover. The true figure was
~64 slices x 15.8 ms, about a second of aggregate CPU.

Two rules:

- A high idle percentage in a scope that wraps a parallel algorithm is not evidence of cheapness.
  It is the expected shape, and it hides the cost precisely where someone would look for it.
- Put a marker INSIDE the parallel body (`APP_SCOPE_PERF_THREAD(..., "Pool Thread")`) whenever
  submitting to a shared pool. It is also the only way to distinguish genuine compute from time
  spent queued behind whatever else uses that pool -- and with one shared pool for asset bakes,
  skinning, and composition, that ambiguity is permanent otherwise.

The timeline settles it in one frame: lanes packed back to back at 100% busy means compute, and
the fix is algorithmic; gaps or late starts mean contention, and the fix is scheduling.

## Estimating a speedup is not measuring one, even when the diagnosis is right

The composition cost was correctly predicted (417M inner iterations, ~53 ms wall vs 45 ms measured)
and the fix was correctly chosen. The predicted MAGNITUDE of the fix was then wrong by an order of
magnitude: per-cell culling was estimated at 100-300x and measured 9.7x at 1600 instances.

The estimate counted only the rejects it removed and ignored the work it cannot remove -- the field
samples for instances that genuinely do reach a voxel. Once the rejects are gone, that floor is the
whole cost, which a cell-size sweep confirmed: 2 voxels per cell performed the same as 4, so the
remaining time was not being spent on culling at all.

Same shape as the closest-point query scaling earlier in the same session, where sqrt(T) was
predicted as log(T). Predict to CHOOSE the fix; measure to size it. And when reporting, give the
measured number rather than the projected one, even when the projection was the reason for doing
the work.

## A regression test must be shown to FAIL on the old behaviour

The first version of the cascade continuity test walked one line outward through the level
boundaries and asserted the sampled distance never jumped by more than a few sampling steps. It
passed. It also passed with the fix reverted, at 2.7x the step against a bound of 8x -- so it was
describing the code, not testing it.

Two things were wrong, and both are general:

- **One sample path finds one crossing.** A single ray crosses each boundary at exactly one point,
  which is almost certainly not the worst one. Sweeping 240 directions found 64x the step where
  one line found 2.7x. Whenever a defect lives on a SURFACE (a boundary, a seam, a silhouette),
  one probe through it is a sample of size one.
- **The bound was picked before the measurement.** 8x was a guess at "clearly broken". Measure the
  broken behaviour first, then place the bound between the two numbers, and assert BOTH sides --
  that the old path exceeds it and the new one does not. The old-path assertion is what stops the
  test quietly becoming vacuous if the fix is later removed.

Cheap trick that made this practical: the old behaviour did not need a second build or a second
composition to measure. `sample_level(find_level(p), p)` IS the hard switch, so both could be
measured from one cascade in the same loop. When a fix adds a step to an existing pipeline, the
pre-fix answer is usually still reachable as an intermediate.

## Cross-fade between levels of a multi-resolution field, do not switch

Each cascade of the global field is composed independently at its own voxel size, so their
isosurfaces do not coincide -- measured, two levels disagreed by up to 4.0 world units where the
finest voxel was 0.25. Selecting the finest covering level and switching at its edge therefore puts
a step discontinuity in a function that is supposed to be 1-Lipschitz.

It is not merely ugly. Every consumer that resolves a surface point by walking down the field lands
somewhere different either side of the boundary, so a writer and a reader straddling it derive
different cache cells and never see each other's entries. That reads as a cache that misses, not as
a geometry problem, which is the same misattribution as the earlier field-space addressing bug.
Continuity is what makes all consumers quote ONE function.

Three constraints on the fade, each of which is a way to get it wrong:

- **Blend, do not extend.** A convex combination of two conservative under-estimates is still an
  under-estimate, so cross-fading preserves the invariant everything rests on. Taking a max, or
  extrapolating, does not.
- **The outermost level must never fade.** Beyond it lies only the "outside every cascade" value,
  which is deliberately enormous. Mixing toward it produces an over-estimate -- the one direction a
  conservative field may never err in, because a trace steps straight through whatever is there.
- **Fade the reported resolution too.** The sampler reports the voxel size of the level that
  answered, because anything scaled to "a voxel" is meaningless otherwise. Inside the band the
  answer is a mixture, so the reported size has to be the same mixture; jumping it at the boundary
  reintroduces exactly the banding that per-level reporting was added to cure.

## Nested parallel algorithms on a shared pool deadlock, they do not merely oversubscribe

`poolstl::par` submits its chunks to one process-wide pool and then BLOCKS the calling thread until
they finish. That is fine from a thread that does not belong to the pool -- two independent callers
just queue behind each other. It is fatal from a thread that does: the pool thread waits on work
queued behind itself, and once every pool thread is doing that nothing can drain the queue.

So parallelising an outer loop is not free when the inner work is already parallel; the two have to
be exclusive. `sdf_bake_threading` makes the caller state which it is, and `poolstl::par.par_if(false)`
is a genuine inline fallback rather than a one-task dispatch, so the serial path costs nothing.

Which level to parallelise is a real choice and worth measuring rather than assuming: for 128 small
submeshes, single threaded was 410 ms, pool-inside-each-bake 50 ms, and submeshes-in-parallel 27 ms.
Inner parallelism cannot fill the pool when each unit of work is small, and pays dispatch overhead
per unit. Hence the threshold in `asset_compiler`: enough submeshes to fill the pool means parallel
outside and serial inside, otherwise the reverse, so a single-submesh prop does not lose its
parallelism.

Test note: a deadlock is invisible in the output -- there is no wrong value to assert on, the build
just stops. Exercise both pairings and compare the fields (`test_parallel_submesh_bake_matches_serial`),
which costs milliseconds and fails loudly instead of hanging the editor on a model with thousands
of parts.


## A failed user experiment is evidence about the KNOB, not about the hypothesis

"I raised Max Total Voxels and Min Voxel Size and the phantom geometry is still there" was read as
ruling out the bake and pointing at the tracer. It did neither. The sizing loop in `mesh_sdf_baker`
only ever GROWS the voxel: both caps are satisfied by coarsening, never by refining. So
`max_total_voxels` is a CEILING rather than a target -- raising it can only stop coarsening, never
reach a finer field than `resolution` already asked for -- and `min_voxel_size` is a FLOOR, so raising
that makes the field coarser still. Neither knob could ever have moved the result in the direction
being tested, and both are named as though they control detail.

Acting on that misreading cost a wrong diagnosis (blaming a cone-radius regression), a shader change
shipped against the wrong cause, and a round trip through the user.

Before treating "I changed X and nothing happened" as evidence against a subsystem, read what X does
and confirm it can move the result in the direction tested. A knob that is a ceiling, a floor, an
unexposed default, or dead code produces exactly the same null result as a correct hypothesis being
wrong -- and the names do not distinguish them. `max_resolution` in the same struct is documented as
a per-axis cap and is never assigned from the importer at all, so it can never fire.

The general form, which the `is_valid()` and `poolstl::par` entries above are also instances of: a
measurement only constrains a hypothesis once you have checked that the thing measured is connected
to the thing hypothesised. Verify the mechanism of the instrument before trusting its null result.

Corollary for diagnostics: the atlas warning pointed at Max Total Voxels, the one setting that cannot
help. A log line that names a knob is an instruction, and naming the wrong one sends every future
reader down the same path. The bake now names Resolution, and says the two must be raised together.

## A harness that cannot call logging code silently excludes it from coverage

`APPLOG_*` expands to `spdlog::get("Log")->log(...)`, and `spdlog::get` returns a NULL shared_ptr
when nothing registered that name. `gi_tests` had a bare `main()` with no logger, so ANY engine
function that logs null-dereferenced the moment a test called it -- a segfault mid-suite with no
failing assertion, which reads as a bug in the code under test.

The consequence is worse than the crash. `bake_mesh_sdf` happens not to log, so the whole GI suite
passed while `mesh::generate_lods_for_load_data` -- which the SDF bake depends on, since fields bake
from LOD 2 by default -- was effectively untestable. A test written against it died instead of
failing, so the coverage gap looked like a deliberate boundary rather than an accident.

Registering a real sink in `main()` fixes it and makes the warnings visible, which is often the thing
a test wants to observe anyway.

Generalisation: when a subsystem has suspiciously little test coverage, check whether the harness is
CAPABLE of calling it before concluding the coverage was a choice. Missing infrastructure and
deliberate scoping look identical from the test list.

## The renderer silently discards geometry the bake takes seriously

A zero-area triangle draws nothing. The GPU rejects it, so a submesh made of slivers looks empty in
the viewport and stays empty when you change its material -- there is no visual evidence it exists.
The SDF bake had no such filter, and a triangle's corners widen the BOUNDS whether or not it has any
surface between them. Bounds pick the voxel size, so one invisible sliver spanning a model produced a
field 10,000 units across with an 86-unit voxel; the unsigned shell floor then made that field a
solid block swallowing a street.

The general trap: "invisible" is a property of the RENDERER, not of the data. Any consumer of mesh
data that is not the rasteriser -- distance fields, collision, lightmap UVs, bounds -- has to state
its own validity rule, because the one the artist and the viewport agreed on was never written down
and does not transfer. Geometry that looks absent is not absent.

Corollary for diagnosis: when an artefact's SIZE has no relationship to anything visible, stop looking
at the visible geometry. The number that was wrong here (extent 10115 in a scene of tens) was never
on screen, and three rounds of hypotheses about content -- material grouping, sparse submeshes,
connected components -- were all attempts to explain it with things that were. Print the numeric
inputs of the pipeline before theorising about its content.

## Do not abandon a hypothesis because the user pushes back on the reasoning for it

The "scattered parts in one submesh" explanation was raised first, dropped when the user said the
parts were separate submeshes, and turned out to be right: the submeshes ARE separate, and submesh 99
is ITSELF a scatter of three lamps over 3804 units. Both statements were true at once. Two further
rounds were spent on theories that were not.

The user was correcting a detail of the reasoning (how the parts are grouped in the inspector), not
the conclusion (the field is sized to a spread rather than to geometry). Treating the correction as
refuting the whole hypothesis threw away the one that fit. When a correction lands, re-check whether
it actually contradicts the conclusion or only the path taken to it -- and say which, out loud, rather
than silently switching tracks.

The measurement that would have settled it in one step was never taken: extent of the geometry versus
extent of the space it occupies. Reach for the number that DISCRIMINATES between the live hypotheses
instead of arguing about which is more plausible. `summarize_connected_components` exists now for
exactly that, and reports 2401x on a scatter versus 1.00x on a solid part.
