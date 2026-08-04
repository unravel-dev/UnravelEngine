$input v_texcoord0

/*
 * Resolves world-space cached radiance into a screen-space indirect diffuse estimate.
 *
 * This is the pass that finally puts the surface cache into the rendered image. Rays leave the
 * shading point, travel through the signed distance field, and read the radiance already stored
 * wherever they land. Nothing is shaded at the hit: the cache holds that answer from previous
 * frames, which is what lets a single ray return a fully lit -- and eventually multi-bounce --
 * result instead of an unlit hit that has to be shaded again.
 *
 * Because the cache is anchored to the world rather than to the screen, a ray may land on
 * geometry that is behind the camera or off the side of the frame and still read a valid value.
 * That is the property a screen-space estimate cannot have at any sample count.
 *
 * The output deliberately matches the SSIL convention exactly -- RGB is a hemispherical indirect
 * diffuse estimate in radiance-mean units, A is the weight with which it replaces the
 * environment probe -- so the existing consumer needs no change and the two remain comparable.
 */

#include "../common.sh"
// DecodeGBufferNormalMetalRoughnessLod lives here, not in common.sh.
#include "../lighting.sh"

#define GI_CACHE_READ_ONLY
#include "gi/radiance_cache.sh"
#include "gi/sdf_common.sh"

SAMPLER2D(s_gi_depth, 8);
SAMPLER2D(s_gi_normal, 9);
/// Diagnostic only. See GiDebugUnshade.
SAMPLER2D(s_gi_base_color, 10);
/// PREVIOUS frame's accumulated luminance moments (mean, mean-square, sample count), for the
/// variance-guided ray budget. Written by the temporal pass, so it lags this pass by one frame;
/// that is exactly what makes it usable here without a feedback hazard.
SAMPLER2D(s_gi_prev_moments, 11);

/// Cancels what the CONSUMER will multiply this pass's output by, so a diagnostic written here
/// arrives on screen as the number it is.
///
/// The output of this pass is indirect diffuse, and fs_pbr_lighting.sh spends it as
/// `mix(irradiance, rgb * PI, a)` and then `DiffuseColor * AO * that` (StandardShadingIndirect).
/// For LIGHTING that is exactly right. For a DIAGNOSTIC it is fatal: every debug view was really
/// showing stage fractions times the surface's own albedo, so black wrought iron read black
/// whatever the rays did, a red awning tinted a cyan reading to dark teal, and only near-white
/// stone reported anything close to the truth. Three separate investigations were run off colours
/// that were mostly paint.
///
/// Dividing by the same factors here makes the modulation cancel. The floor keeps a near-black
/// albedo from exploding rather than merely being unreadable, so a dark channel on dark paint
/// stays honest about being unmeasurable there. Exposure and tonemapping still apply and are
/// monotonic, so compare channels against each other, not against an absolute value.
vec3 GiDebugUnshade(vec3 value, vec2 uv)
{
	GBufferDataColorAndAO color_data = DecodeGBufferColorAndAOLod(uv, s_gi_base_color, 0.0);
	vec3 modulation = color_data.base_color * max(color_data.ambient_occlusion, 1e-3);
	return value / max(modulation * PI, vec3_splat(1e-3));
}

/// x = ray count, y = max trace distance, z = normal bias in VOXELS of the answering field,
/// w = frame index.
uniform vec4 u_gi_resolve_params;
#define u_gi_ray_count    int(u_gi_resolve_params.x)
#define u_gi_max_distance u_gi_resolve_params.y
#define u_gi_normal_bias  u_gi_resolve_params.z
#define u_gi_frame_index  uint(u_gi_resolve_params.w)

/// x = near field distance, y = max steps, z = surface bias in voxels, w = step relaxation.
uniform vec4 u_gi_resolve_trace;
#define u_gi_trace_near_field u_gi_resolve_trace.x
#define u_gi_trace_max_steps  int(u_gi_resolve_trace.y)
#define u_gi_trace_bias       u_gi_resolve_trace.z
#define u_gi_trace_relaxation u_gi_resolve_trace.w

/// xyz = camera position, w = intensity applied to the cached bounce.
uniform vec4 u_gi_resolve_camera;
#define u_gi_intensity u_gi_resolve_camera.w

/// x = non-zero to interpolate the cache across neighbouring cells instead of point sampling one.
uniform vec4 u_gi_resolve_filter;
#define u_gi_cache_interpolate     u_gi_resolve_filter.x
#define u_gi_occlude_on_cache_miss (u_gi_resolve_filter.y > 0.0)
/// z = view distance at which the near field has fully faded out; 0 disables the fade.
#define u_gi_near_field_fade       u_gi_resolve_filter.z
/// How far along its OWN DIRECTION a gather ray starts, in voxels. See
/// gi_resolve_pass::settings::ray_start_voxels: this does the self-intersection job a normal
/// offset was doing, without moving the shading point off its surface.
#define u_gi_ray_start             u_gi_resolve_filter.w

/// x != 0 replaces the radiance output with a per-ray DIAGNOSTIC, so the three ways a gather
/// ray can fail are separable in one view instead of inferred from the lit image:
///   R = fraction of rays that HIT geometry at all (low means rays escape to sky)
///   G = fraction whose hit could be ADDRESSED (low means SdfResolveSurfacePoint failed)
///   B = fraction that FOUND a cache entry there (low means the lookup misses)
/// A ray contributes light only when all three succeed, so whichever channel is dark is the
/// stage at fault. Every hypothesis about darkening is a claim about one of these numbers.
uniform vec4 u_gi_resolve_debug;
#define u_gi_debug_mode    int(u_gi_resolve_debug.x)
#define u_gi_debug_enabled (u_gi_debug_mode > 0)
/// y = ray count for SETTLED pixels (0 disables the adaptive budget),
/// z = relative-sigma threshold below which a pixel counts as settled.
#define u_gi_adaptive_min_rays int(u_gi_resolve_debug.y)
#define u_gi_adaptive_sigma    u_gi_resolve_debug.z

/// Builds an arbitrary orthonormal basis around a normal without a branch on the degenerate axis.
void GiBuildBasis(vec3 n, out vec3 t, out vec3 b)
{
	float s = n.z >= 0.0 ? 1.0 : -1.0;
	float a = -1.0 / (s + n.z);
	float c = n.x * n.y * a;
	t = vec3(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
	b = vec3(c, s + n.y * n.y * a, -n.y);
}

/// Cosine-weighted hemisphere direction. Cosine weighting is what makes the plain MEAN of the
/// samples an unbiased estimate of irradiance/PI, so no per-sample cosine term is needed.
vec3 GiCosineDirection(vec3 n, float u1, float u2)
{
	vec3 t, b;
	GiBuildBasis(n, t, b);
	float r = sqrt(u1);
	float phi = 6.2831853 * u2;
	return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(0.0, 1.0 - u1)));
}

void main()
{
	vec2 uv = v_texcoord0;
	float depth = texture2DLod(s_gi_depth, uv, 0.0).x;
	// Sky: no surface to gather for. Alpha 0 leaves the consumer on its environment probe.
	if(depth >= 1.0)
	{
		gl_FragColor = vec4_splat(0.0);
		return;
	}
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(depth)));
	vec3 world_position = clipToWorld(u_invViewProj, clip);
	GBufferDataNormalMetalRoughness nd = DecodeGBufferNormalMetalRoughnessLod(uv, s_gi_normal, 0.0);
	vec3 world_normal = nd.world_normal;
	if(dot(world_normal, world_normal) < 0.5)
	{
		gl_FragColor = vec4_splat(0.0);
		return;
	}
	world_normal = normalize(world_normal);
	// Lift off the surface before tracing, in VOXELS of the field that answers here rather than in
	// world units. A ray starting on the isosurface reads a distance of zero and stops immediately,
	// reporting its own origin as an occluder -- which is what surface acne is.
	//
	// How far out is far enough is set by the field's own resolution, not by any world scale: the
	// trace accepts a hit within `surface_bias` voxels, so the lift has to clear that. The cascade's
	// voxel runs from 0.25 m at level 0 to 2 m at the outer level, an eightfold range across one
	// view, so a single world distance cannot work everywhere. It is either too small far away,
	// where the acne survives, or far too large near by, where it lifts rays clean over the contact
	// detail they exist to find. Measured on two scenes before this changed: 0.1 sufficed where
	// everything sat inside level 0, while a view spanning levels 1-3 still needed 1.0.
	float origin_voxel;
	// The DISTANCE matters here, not only the voxel size. It says how far the cascade thinks this
	// point is from its own isosurface, which is exactly the quantity a lift has to clear -- and it
	// was already being computed and discarded.
	float origin_distance = SdfSampleClipmapEx(world_position, origin_voxel);
	// Floored: with no cascade resident the reported size collapses to an epsilon, and a zero lift
	// puts every ray back on the surface it started from.
	origin_voxel = max(origin_voxel, 0.01);
	// Probed again a lift-length outward, taking the COARSER of the two. A shading point just
	// inside a fine cascade level has rays that immediately cross into the next one, and both of
	// the things the lift must clear grow with that level's voxel: the acceptance radius, which is
	// surface_bias voxels, and -- larger -- the displacement between the cascade's isosurface and
	// the rendered triangle, since a coarse level represents the wall a whole voxel or more away
	// from where it actually is. Sizing the lift from the fine level alone leaves the ray starting
	// inside the coarse level's solid, which is the acne that reappears on crossing a cascade
	// boundary rather than uniformly across the view.
	float outward_voxel;
	SdfSampleClipmapEx(world_position + world_normal * (u_gi_normal_bias * origin_voxel), outward_voxel);
	origin_voxel = max(origin_voxel, outward_voxel);
	// MEASURED, not guessed. The lift needed is however far this point is from where the cascade
	// thinks the surface is, plus the margin a trace needs to not re-hit it -- and the field just
	// reported the first term. A fixed multiple of the voxel has to assume the worst case for every
	// pixel, because it cannot tell a point the cascade already agrees with from one displaced a
	// whole voxel; that worst case is what forced the bias up to metres and lifted every ray clean
	// over the contact detail it exists to find.
	//
	// Where the cascade already places the surface correctly this is ZERO and the shading point is
	// not moved at all. Where a coarse level puts the wall a voxel away it pushes exactly that far
	// and no further, so the cost is paid only by the pixels that need it.
	//
	// A ray-start offset cannot replace this: it clears a perpendicular distance of t * cos(theta),
	// so a grazing ray needs an unbounded start to clear the same gap. The normal direction is the
	// only one where a bounded push works for every ray at once.
	// The lift does two jobs and only ONE of them may be held to the finest level.
	//
	//  - The MARGIN has to clear this trace's own hit acceptance, which is surface_bias voxels OF
	//    THE LEVEL THAT ANSWERS -- 0.025 m at level 0 but 0.2 m at the coarsest. Clamping it to the
	//    finest voxel makes it smaller than the acceptance radius out there, so the ray registers a
	//    hit immediately however much the displacement term contributes. That presents as darkening
	//    that clears on near surfaces first as the bias is raised and holds out on far ones.
	//  - The DISPLACEMENT between the rendered triangle and the cascade isosurface is measured, not
	//    guessed, so it needs no allowance at all: -origin_distance is exactly how far inside the
	//    point sits, and zero when it is already clear.
	//
	// Splitting them is what lets the bias stay SMALL. It no longer has to cover the worst-case
	// displacement for every pixel, so a value around half a voxel is enough -- which is why the
	// finest-voxel clamp is gone from here.
	float margin = u_gi_normal_bias * origin_voxel;
	float lift = max(0.0, -origin_distance) + margin;
	vec3 origin = world_position + world_normal * lift;
	// The shading point's OWN surface resolve -- computed LAZILY, only where the cascade says the
	// point sits inside its isosurface. That is the one case the resolve is load bearing for: the
	// buried-origin escape below, and the exact-key self-read rejection that guards it. For the
	// majority of pixels, which read on or outside the isosurface, the geometric same-plane
	// rejection in the ray loop already covers self-reads, and the resolve's fourteen cascade
	// samples bought nothing measurable. Verify with debug mode 3's R channel if in doubt: the
	// self-read fraction should not move when this laziness is toggled.
	SdfSurfacePoint own_surface;
	own_surface.valid = false;
	if(origin_distance < 0.0)
	{
		own_surface = SdfResolveSurfacePoint(world_position);
	}
	// A point INSIDE the isosurface cannot always be freed by a lift along the G-buffer normal.
	// The lift assumes the phantom solid lies along -normal, but on relief at voxel scale -- a
	// cornice, a window reveal -- the cascade's bulge overhangs LATERALLY: the wall normal points
	// out of the facade while the solid extends down from the ledge above, so however far the
	// point is pushed it stays buried, every ray registers a hit at t ~ 0, and the pixel reads
	// black. That is the acne that gets WORSE as the camera approaches: close in, level 0 answers,
	// and detail that a coarse level flattened away becomes exactly this kind of bulge.
	//
	// The surface resolve already solves this: its Newton steps move along the GRADIENT, which by
	// definition is the direction out of the solid, wherever the solid lies. So when the cascade
	// says the point is inside, start rays from the resolved isosurface point instead -- which is
	// also the point every writer and reader keys the cache by, so it is the most consistent
	// origin available, not merely an escape hatch. Guarded on the resolved facing agreeing with
	// the G-buffer, so converging through thin geometry onto its far side falls back to the lift.
	if(origin_distance < 0.0 && own_surface.valid && dot(own_surface.normal, world_normal) > 0.0)
	{
		origin = own_surface.position + own_surface.normal * margin;
		lift = length(origin - world_position);
	}
	// The shading point's OWN cache keys, for the self-read rejection in the ray loop.
	//
	// Mode 2 asks "did the ray hit close by", which is a proxy with an arbitrary threshold. This is
	// the actual question: did the ray come back and read the very entry that is being shaded? Keys
	// are exact, so there is nothing to tune and no threshold to argue about. Resolved the same way
	// the writer resolves, or the two would disagree for reasons unrelated to self-reading.
	//
	// BOTH levels when the point sits in the level cross-fade band. The writer inserts the surface
	// at both levels there, so the entry being shaded exists twice under two keys; a ray whose hit
	// lands a whisker further out re-keys to the next level and would read the coarser copy of this
	// very surface with the primary key none the wiser.
	uint own_key = GI_CACHE_EMPTY_KEY;
	uint own_key_far = GI_CACHE_EMPTY_KEY;
	if(own_surface.valid)
	{
		float own_blend;
		uint own_level = GiCacheLevelEx(own_surface.position, u_gi_resolve_camera.xyz, own_blend);
		uint own_face = GiQuantizeNormal(own_surface.normal);
		own_key = GiCacheKeyForFace(own_surface.position, own_face, own_level);
		if(own_blend > 0.0)
		{
			own_key_far = GiCacheKeyForFace(own_surface.position, own_face, own_level + 1u);
		}
	}
	// The per-instance tier faded out with VIEW distance. It exists for contact fidelity, and
	// contact detail is only visible near the camera: a distant pixel renders a cascade-scale
	// area, so tracing its first metres against exact mesh fields buys nothing the half-res
	// gather can display -- while remaining the single most expensive thing in the GI frame.
	// The fade starts at half the fade distance and reaches zero at it, so the handover is a
	// gradient rather than a line across the ground.
	float near_field = u_gi_trace_near_field;
	if(u_gi_near_field_fade > 0.0)
	{
		float view_distance = length(world_position - u_gi_resolve_camera.xyz);
		float fade_start = 0.5 * u_gi_near_field_fade;
		near_field *= saturate((u_gi_near_field_fade - view_distance) /
		                       max(u_gi_near_field_fade - fade_start, 1e-3));
	}
	float debug_self_reads = 0.0;
	// Decorrelate the sample pattern per pixel AND per frame, so the residual error is noise
	// that temporal accumulation can average away rather than a fixed pattern that it cannot.
	uint pixel_seed = GiHashCombine(GiHashUint(uint(gl_FragCoord.x)), uint(gl_FragCoord.y));
	uint seed = GiHashCombine(pixel_seed, u_gi_frame_index);
	vec3 sum = vec3_splat(0.0);
	float resolved = 0.0;
	float debug_hit = 0.0;
	float debug_addressed = 0.0;
	// Rays that read an actual cache ENTRY, which is NOT the same as `resolved`. With
	// u_gi_occlude_on_cache_miss on, a miss increments `resolved` too -- deliberately, since the trace
	// did establish that something is there -- so the two differ by exactly the misses. Reporting
	// `resolved` as though it were "found" hid the one stage that can produce pure black at full
	// weight, and hid it precisely when the switch that causes it is on.
	float debug_found = 0.0;
	// Where the hit LANDED, not merely that there was one. A ray that re-hits the surface it
	// started on looks identical to one that travelled twenty metres in every stage counter, and
	// that is the difference every bias knob actually moves.
	float debug_near_hits = 0.0;
	float debug_total_t = 0.0;
	int ray_count = max(u_gi_ray_count, 1);
	// Variance-guided ray budget: spend rays where the estimate is still noisy, not where it has
	// already settled. The temporal pass accumulates per-pixel luminance moments and a sample
	// count; a pixel whose history is deep and whose relative deviation is small has converged,
	// and tracing four rays into it re-measures a number that is already known. Most pixels of a
	// mostly-static view are in that state, so this is a large saving that costs quality nowhere:
	// disocclusions reset the count and lighting changes raise the variance, and both immediately
	// restore the full budget exactly where it is needed.
	//
	// The absolute-variance clause keeps DARK settled pixels settled: relative sigma divides by
	// the mean, so near-black pixels would otherwise read as "noisy" forever and keep full rays.
	if(u_gi_adaptive_min_rays > 0)
	{
		vec4 prev_moments = texture2DLod(s_gi_prev_moments, uv, 0.0);
		float accumulated = prev_moments.z;
		float mean = prev_moments.x;
		float variance = max(prev_moments.y - mean * mean, 0.0);
		float relative_sigma = sqrt(variance) / max(mean, 1e-3);
		if(accumulated >= 8.0 && (relative_sigma < u_gi_adaptive_sigma || variance < 1e-6))
		{
			ray_count = min(ray_count, u_gi_adaptive_min_rays);
		}
	}
	for(int i = 0; i < ray_count; ++i)
	{
		seed = GiHashUint(seed);
		float u1 = float(seed & 0xFFFFu) / 65535.0;
		seed = GiHashUint(seed);
		float u2 = float(seed & 0xFFFFu) / 65535.0;
		vec3 direction = GiCosineDirection(world_normal, u1, u2);
		// Start the ray along its OWN direction rather than pushing the origin further out along the
		// normal. Both skip the region where the ray would hit the surface it started on, but a
		// normal offset MOVES THE SHADING POINT: the point then sees past nearby geometry, which is
		// over-lighting that no amount of tuning removes because it is what the offset does. Starting
		// along the ray leaves the point exactly on its surface, so occlusion stays correct.
		//
		// It is also cheaper for the same immunity. What has to be cleared is a perpendicular
		// distance -- the gap between the cascade isosurface and the rendered triangle -- and a
		// normal offset pays it in full for every ray, including the grazing ones that need it least.
		vec3 ray_origin = origin + direction * (u_gi_ray_start * origin_voxel);
		SdfRayHit hit = SdfTraceRay(ray_origin, direction, u_gi_max_distance, near_field,
		                            u_gi_trace_max_steps, u_gi_trace_bias, u_gi_trace_relaxation,
		                            false);
		// Escaped the scene. Deliberately contributes nothing and does NOT count toward the
		// weight, which leaves the consumer's own environment probe covering that fraction of
		// the hemisphere. Sampling the environment here instead would compute the same quantity
		// twice and force this pass to run after the irradiance pass it would depend on.
		if(!hit.hit)
		{
			continue;
		}
		debug_hit += 1.0;
		debug_total_t += hit.t;
		// "Near" measured in VOXELS of the field that answered, because that is the scale the
		// isosurface can be displaced by, and so the scale a self-hit happens at.
		if(hit.t < 4.0 * origin_voxel)
		{
			debug_near_hits += 1.0;
		}
		// Same resolve the writer used. Addressing the cache from a raw hit misses, because the
		// hit sits short of an isosurface that is itself offset from the rendered geometry.
		SdfSurfacePoint surface = SdfResolveSurfacePoint(ray_origin + direction * hit.t);
		// Nothing to look up: the hit is outside every cascade level, so no address can be derived
		// for it. Treated as an unresolved ray rather than as darkness -- it lowers the weight and
		// leaves the consumer's environment probe covering that part of the hemisphere, which is
		// the same conservative choice a cache miss makes below.
		if(!surface.valid)
		{
			continue;
		}
		debug_addressed += 1.0;
		// A ray that resolves back to the entry being shaded measured nothing: it is the shading
		// point reading itself. cs_gi_cache_update.sc rejects exactly this case for the bounce -- see
		// the note on `hit_slot == slot` there -- and the gather needs it for that reason and one
		// more.
		//
		// Counting such a ray is WORSE than losing it. It contributes this surface's own radiance,
		// which is near black on anything not directly lit, AND it takes full weight, so the
		// consumer's mix(probe, rgb * PI, a) drives a toward 1 and replaces the environment probe
		// with that darkness. The symptom is black patches on flat surfaces, worst near the camera:
		// every bias here is a count of cascade VOXELS, so a finer level -- closer camera, or a
		// higher clipmap resolution -- shrinks the world-space clearance while the near field's own
		// hit acceptance, which is a count of MESH voxels, does not move at all.
		//
		// Skipped without counting toward `resolved`, matching the bounce's `bounce_samples`: a
		// failure to sample, not a sample of darkness. That fraction of the hemisphere goes back to
		// the consumer's probe, which is what an unmeasured direction is worth.
		// A hit whose resolved surface lies in the PLANE of the surface being shaded, facing the
		// same way, is the shading surface reading itself -- whichever cell it lands in. On a flat
		// surface no ray in the upper hemisphere can geometrically hit that surface again, so such
		// a hit exists only because the ray grazed the cascade's displaced isosurface and the cone
		// acceptance caught it. The key test below can only reject the ONE cell the shading point
		// occupies, while a grazing ray lands one or two cells away laterally: diagnostics mode 2
		// showed those as near hits, mode 1 showed them FOUND, and what they found was a
		// neighbouring cell of this very floor -- which in shadow is near black, and it entered at
		// full weight. That is the black patch that survived the exact-key rejection.
		//
		// The plane tolerance is in voxels of the level answering at the ORIGIN, because the
		// displacement that manufactures these hits is that level's. The facing gate keeps every
		// genuine perpendicular occluder: a wall scores ~0 against a floor. An opposite-facing
		// surface (a ceiling seen from the floor) scores negative and is kept as the occluder it
		// is. Skipped WITHOUT counting, like the key rejection: it is a failure to measure, and
		// that fraction of the hemisphere belongs to the consumer's probe.
		float self_plane = dot(surface.position - world_position, world_normal);
		if(abs(self_plane) < 2.0 * origin_voxel && dot(surface.normal, world_normal) > 0.7)
		{
			debug_self_reads += 1.0;
			continue;
		}
		if(own_key != GI_CACHE_EMPTY_KEY)
		{
			float hit_blend;
			uint hit_level = GiCacheLevelEx(surface.position, u_gi_resolve_camera.xyz, hit_blend);
			uint hit_key = GiCacheKeyForFace(surface.position, GiQuantizeNormal(surface.normal), hit_level);
			if(hit_key == own_key || (own_key_far != GI_CACHE_EMPTY_KEY && hit_key == own_key_far))
			{
				debug_self_reads += 1.0;
				continue;
			}
		}
		// Level and its cross-fade weight together: crossing a level boundary re-keys the surface,
		// so a reader that consults only one level loses every entry built at the other one and
		// falls back to the environment probe -- GI dimming as the camera closes in.
		// Interpolated across the four cells bracketing the hit in its tangent plane, not point
		// sampled from one. A cell is metres across where a pixel is millimetres, so a point lookup
		// makes this gather piecewise constant at cell scale -- blocks that shift as the cascade
		// re-snaps or the level steps. Those are bias rather than noise, so no amount of temporal
		// accumulation averages them out and no luminance edge stop can remove them without
		// removing real lighting detail too.
		//
		// Explicit if/else rather than a ternary: both arms write through an out parameter, and a
		// ternary whose arms have side effects is not guaranteed to evaluate only one of them.
		vec3 cached;
		bool found;
		if(u_gi_cache_interpolate > 0.0)
		{
			found = GiCacheGatherLevels(surface.position, surface.normal, u_gi_resolve_camera.xyz, cached);
		}
		else
		{
			float ignored_blend;
			uint level = GiCacheLevelEx(surface.position, u_gi_resolve_camera.xyz, ignored_blend);
			found = GiCacheGatherPoint(surface.position, surface.normal, level, cached);
		}
		// A miss means this cell has not been LIT yet. It does not mean the cell is dark -- but
		// the ray did hit geometry, so it does mean the environment is occluded in this direction,
		// and the two are not the same claim.
		//
		// Skipping the ray hands that fraction of the hemisphere back to the consumer's SH probe,
		// which is a light leak that never resolves: a sealed room with no light in it keeps every
		// ray missing, keeps the weight at zero, and stays lit by the sky through solid walls.
		// Counting the miss as resolved-with-zero instead says what the trace actually established
		// -- something is there -- and lets such a room converge to black.
		//
		// The cost is that a cache which has not filled in yet reads dark rather than
		// probe-coloured, which is why this is a switch and not a rewrite: warm-up is transient and
		// self-correcting, while the leak is permanent, so occluding is the better DEFAULT rather
		// than the only choice.
		if(!found)
		{
			// Occlude-on-miss is gated on the hit being FURTHER than the scale the field can
			// actually resolve. At contact range a hit is the least trustworthy thing this trace
			// produces -- it may be the displaced isosurface, a cone-acceptance catch, or an edge
			// cell insertion never reaches -- and the stage diagnostic shows exactly where those
			// live: yellow fringes hugging curbs and crevices, each one black at full weight in
			// the lit image. Handing that fraction back to the probe costs a transient of light
			// where a genuine contact occluder has no entry yet, which insertion repairs within
			// frames; the occlusion stamp made a PERMANENT black rim out of cells that can never
			// be inserted at all. The sealed-room guarantee is untouched: room-scale hits are far
			// beyond two voxels, so they still occlude.
			if(u_gi_occlude_on_cache_miss && hit.t >= 2.0 * origin_voxel)
			{
				resolved += 1.0;
			}
			continue;
		}
		sum += cached * u_gi_intensity;
		resolved += 1.0;
		debug_found += 1.0;
	}
	if(u_gi_debug_enabled)
	{
		float inv_rays = 1.0 / float(ray_count);
		if(u_gi_debug_mode >= 3)
		{
			// Mode 3: the three numbers every remaining theory is about.
			//   R = fraction of rays REJECTED as reading the entry being shaded -- EXACT, by key, no
			//       threshold. These no longer reach the output, so this is now how much of the
			//       hemisphere the gather failed to measure and handed back to the probe, not how
			//       much darkness it wrote.
			//   G = the lift actually applied, in voxels. Near zero here means the adaptive term is
			//       not firing; large here with red bright means the lift is not the mechanism.
			//   B = the cascade's signed distance at the shading point, in voxels, mid grey = exactly
			//       on the isosurface. This is the INPUT the adaptive lift is derived from, so if it
			//       reads positive where the surface is visibly displaced, that input is wrong.
			vec3 debug_rgb = vec3(debug_self_reads * inv_rays,
			                      saturate(lift / max(origin_voxel, 1e-4)),
			                      saturate(0.5 + origin_distance / max(2.0 * origin_voxel, 1e-4)));
			gl_FragColor = vec4(GiDebugUnshade(debug_rgb, uv), 1.0);
			return;
		}
		if(u_gi_debug_mode >= 2)
		{
			// Mode 2: WHERE the rays landed.
			//   R = fraction that hit within 4 voxels of the origin -- a self-hit on the surface
			//       being shaded, which returns that surface's OWN radiance as its incoming light.
			//   G = mean hit distance, scaled so mid grey is a tenth of the ray budget.
			//   B = fraction that resolved at all, for reference.
			// Red means the gather is reading itself; that is a feedback loop no ray count can fix
			// and it is what a larger origin offset papers over.
			//
			// R is SCALE RELATIVE and reads very differently near and far. Four voxels is 1 m at
			// cascade level 0 but 8 m at the coarsest, so on distant ground in a narrow street almost
			// every ray qualifies as a "near" hit without anything being wrong. Trust this channel
			// where the surface is close enough to sit in level 0; treat it as noise beyond that.
			float mean_t = debug_hit > 0.0 ? debug_total_t / debug_hit : 0.0;
			vec3 debug_rgb = vec3(debug_near_hits * inv_rays,
			                      saturate(mean_t / max(u_gi_max_distance * 0.1, 1e-3)),
			                      resolved * inv_rays);
			gl_FragColor = vec4(GiDebugUnshade(debug_rgb, uv), 1.0);
			return;
		}
		// Mode 1: the three stages, and B is FOUND rather than `resolved`.
		//
		// It reported `resolved` until this was noticed, which with u_gi_occlude_on_cache_miss on
		// counts a miss as a success -- so the stage that writes black at full weight was
		// indistinguishable from the stage that writes light. White here now means the rays really did
		// read cached radiance and any darkness is IN the cache; yellow (R and G high, B low) means
		// they hit addressable geometry the cache has never lit, and every one of those contributed
		// zero radiance at full weight. No lift, bias or ray-start value moves the second case, since
		// nothing about it is a question of where the ray started.
		vec3 debug_rgb = vec3(debug_hit * inv_rays, debug_addressed * inv_rays, debug_found * inv_rays);
		gl_FragColor = vec4(GiDebugUnshade(debug_rgb, uv), 1.0);
		return;
	}
	if(resolved <= 0.0)
	{
		gl_FragColor = vec4_splat(0.0);
		return;
	}
	// RGB is the mean over rays that actually resolved; A is the fraction that did. The consumer
	// computes mix(probe, rgb * PI, a), so an unresolved ray costs weight rather than energy.
	gl_FragColor = vec4(sum / resolved, resolved / float(ray_count));
}
