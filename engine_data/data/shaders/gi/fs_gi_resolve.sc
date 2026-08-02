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
#define u_gi_cache_interpolate u_gi_resolve_filter.x

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
	SdfSampleClipmapEx(world_position, origin_voxel);
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
	vec3 origin = world_position + world_normal * (u_gi_normal_bias * origin_voxel);
	// Decorrelate the sample pattern per pixel AND per frame, so the residual error is noise
	// that temporal accumulation can average away rather than a fixed pattern that it cannot.
	uint pixel_seed = GiHashCombine(GiHashUint(uint(gl_FragCoord.x)), uint(gl_FragCoord.y));
	uint seed = GiHashCombine(pixel_seed, u_gi_frame_index);
	vec3 sum = vec3_splat(0.0);
	float resolved = 0.0;
	int ray_count = max(u_gi_ray_count, 1);
	for(int i = 0; i < ray_count; ++i)
	{
		seed = GiHashUint(seed);
		float u1 = float(seed & 0xFFFFu) / 65535.0;
		seed = GiHashUint(seed);
		float u2 = float(seed & 0xFFFFu) / 65535.0;
		vec3 direction = GiCosineDirection(world_normal, u1, u2);
		SdfRayHit hit = SdfTraceRay(origin, direction, u_gi_max_distance, u_gi_trace_near_field,
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
		// Same resolve the writer used. Addressing the cache from a raw hit misses, because the
		// hit sits short of an isosurface that is itself offset from the rendered geometry.
		SdfSurfacePoint surface = SdfResolveSurfacePoint(origin + direction * hit.t);
		// Nothing to look up: the hit is outside every cascade level, so no address can be derived
		// for it. Treated as an unresolved ray rather than as darkness -- it lowers the weight and
		// leaves the consumer's environment probe covering that part of the hemisphere, which is
		// the same conservative choice a cache miss makes below.
		if(!surface.valid)
		{
			continue;
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
		// A miss contributes NOTHING rather than black. It means this cell has not been lit yet,
		// not that it is dark, and averaging in a zero would darken exactly the places the cache
		// has yet to reach. Leaving it out of both sums lowers the weight instead, so the
		// consumer falls back toward its environment probe there -- the conservative direction.
		if(!found)
		{
			continue;
		}
		sum += cached * u_gi_intensity;
		resolved += 1.0;
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
