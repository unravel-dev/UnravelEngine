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
/// The FINEST cascade voxel, which the voxel-relative normal bias is held to. Cascade voxels
/// span 0.25 m to 2 m, so an unbounded bias lifts distant shading points METRES off their
/// surfaces and the far field reads brighter than the near one.
#define u_gi_finest_voxel          u_gi_resolve_filter.z
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
	float lift_target = u_gi_normal_bias * origin_voxel;
	float lift = max(0.0, lift_target - origin_distance);
	vec3 origin = world_position + world_normal * lift;
	// Decorrelate the sample pattern per pixel AND per frame, so the residual error is noise
	// that temporal accumulation can average away rather than a fixed pattern that it cannot.
	uint pixel_seed = GiHashCombine(GiHashUint(uint(gl_FragCoord.x)), uint(gl_FragCoord.y));
	uint seed = GiHashCombine(pixel_seed, u_gi_frame_index);
	vec3 sum = vec3_splat(0.0);
	float resolved = 0.0;
	float debug_hit = 0.0;
	float debug_addressed = 0.0;
	// Where the hit LANDED, not merely that there was one. A ray that re-hits the surface it
	// started on looks identical to one that travelled twenty metres in every stage counter, and
	// that is the difference every bias knob actually moves.
	float debug_near_hits = 0.0;
	float debug_total_t = 0.0;
	int ray_count = max(u_gi_ray_count, 1);
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
		SdfRayHit hit = SdfTraceRay(ray_origin, direction, u_gi_max_distance, u_gi_trace_near_field,
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
			if(u_gi_occlude_on_cache_miss)
			{
				resolved += 1.0;
			}
			continue;
		}
		sum += cached * u_gi_intensity;
		resolved += 1.0;
	}
	if(u_gi_debug_enabled)
	{
		float inv_rays = 1.0 / float(ray_count);
		if(u_gi_debug_mode >= 2)
		{
			// Mode 2: WHERE the rays landed.
			//   R = fraction that hit within 4 voxels of the origin -- a self-hit on the surface
			//       being shaded, which returns that surface's OWN radiance as its incoming light.
			//   G = mean hit distance, scaled so mid grey is a tenth of the ray budget.
			//   B = fraction that resolved at all, for reference.
			// Red means the gather is reading itself; that is a feedback loop no ray count can fix
			// and it is what a larger origin offset papers over.
			float mean_t = debug_hit > 0.0 ? debug_total_t / debug_hit : 0.0;
			gl_FragColor = vec4(debug_near_hits * inv_rays,
			                    saturate(mean_t / max(u_gi_max_distance * 0.1, 1e-3)),
			                    resolved * inv_rays,
			                    1.0);
			return;
		}
		gl_FragColor = vec4(debug_hit * inv_rays, debug_addressed * inv_rays, resolved * inv_rays, 1.0);
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
