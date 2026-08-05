#ifndef __GI_GATHER_COMMON_SH__
#define __GI_GATHER_COMMON_SH__

/*
 * The gather ray: origin preparation and the full per-ray pipeline -- trace, resolve, self-read
 * rejection, cache lookup -- shared by every consumer that turns cache radiance into incoming
 * light.
 *
 * Extracted from fs_gi_resolve.sc when the probe gather was added, for the same reason every
 * shared piece of this system lives in one file: the per-pixel path and the probe path MUST
 * gather identically, or A/B comparisons between them measure the drift instead of the
 * architecture. Include radiance_cache.sh (GI_CACHE_READ_ONLY) and sdf_common.sh first.
 *
 * The reasoning behind each step -- the measured lift, the buried-origin escape, the two
 * rejections, occlude-on-miss and its contact gate -- is documented at length in the git history
 * of fs_gi_resolve.sc and in tasks/lessons.md; the comments here state what each step is, not
 * the full case for it.
 */

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

/// x = non-zero to interpolate the cache across neighbouring cells instead of point sampling one,
/// y = non-zero to treat a cache miss on a hit as occlusion, z = near-field fade distance
/// (0 disables), w = ray start along its own direction in voxels.
uniform vec4 u_gi_resolve_filter;
#define u_gi_cache_interpolate     u_gi_resolve_filter.x
#define u_gi_occlude_on_cache_miss (u_gi_resolve_filter.y > 0.0)
#define u_gi_near_field_fade       u_gi_resolve_filter.z
#define u_gi_ray_start             u_gi_resolve_filter.w

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

/// Everything a gather ray needs that is a property of the LAUNCH POINT rather than of the ray:
/// computed once per pixel or per probe, shared by every ray leaving it.
struct GiGatherSetup
{
	vec3 world_position;
	vec3 world_normal;
	/// Ray origin after the measured lift (or the buried-origin escape).
	vec3 origin;
	/// Voxel of the cascade level answering at the launch point, floored away from zero.
	float origin_voxel;
	/// Cascade's signed distance at the launch point. Diagnostic.
	float origin_distance;
	/// World distance the origin was moved off the raster surface. Diagnostic.
	float lift;
	/// Per-launch-point near field, after the view-distance fade.
	float near_field;
	/// The launch point's own cache keys, for the exact self-read rejection.
	uint own_key;
	uint own_key_far;
};

/**
 * Prepares the shared launch state: the measured lift off the cascade isosurface, the
 * buried-origin escape through the field gradient, the self-read keys, and the near-field fade.
 */
GiGatherSetup GiPrepareGather(vec3 world_position, vec3 world_normal)
{
	GiGatherSetup s;
	s.world_position = world_position;
	s.world_normal = world_normal;
	// Lift off the surface before tracing, in VOXELS of the field that answers here. A ray
	// starting on the isosurface reads a distance of zero and stops immediately -- surface acne.
	// The DISTANCE is measured too: -origin_distance is exactly how far inside the isosurface the
	// point sits, so the lift pays only what each pixel actually needs.
	float origin_voxel;
	float origin_distance = SdfSampleClipmapEx(world_position, origin_voxel);
	origin_voxel = max(origin_voxel, 0.01);
	// Probed again a lift-length outward, taking the COARSER of the two, so a launch point just
	// inside a fine level still clears the next level's acceptance and displacement.
	float outward_voxel;
	SdfSampleClipmapEx(world_position + world_normal * (u_gi_normal_bias * origin_voxel), outward_voxel);
	origin_voxel = max(origin_voxel, outward_voxel);
	float margin = u_gi_normal_bias * origin_voxel;
	float lift = max(0.0, -origin_distance) + margin;
	vec3 origin = world_position + world_normal * lift;
	// The launch point's OWN surface resolve -- lazily, only where the cascade reports the point
	// buried. That is where it is load bearing: the gradient-following escape below (a normal
	// lift cannot exit a laterally overhanging bulge), and the exact-key rejection that guards it.
	SdfSurfacePoint own_surface;
	own_surface.valid = false;
	if(origin_distance < 0.0)
	{
		own_surface = SdfResolveSurfacePoint(world_position);
		if(own_surface.valid && dot(own_surface.normal, world_normal) > 0.0)
		{
			origin = own_surface.position + own_surface.normal * margin;
			lift = length(origin - world_position);
		}
	}
	s.origin = origin;
	s.origin_voxel = origin_voxel;
	s.origin_distance = origin_distance;
	s.lift = lift;
	// Own cache keys, BOTH levels inside the cross-fade band: the writer inserts there twice, and
	// a hit re-keying to the next level would read the coarser copy of this very surface.
	s.own_key = GI_CACHE_EMPTY_KEY;
	s.own_key_far = GI_CACHE_EMPTY_KEY;
	if(own_surface.valid)
	{
		float own_blend;
		uint own_level = GiCacheLevelEx(own_surface.position, u_gi_resolve_camera.xyz, own_blend);
		uint own_face = GiQuantizeNormal(own_surface.normal);
		s.own_key = GiCacheKeyForFace(own_surface.position, own_face, own_level);
		if(own_blend > 0.0)
		{
			s.own_key_far = GiCacheKeyForFace(own_surface.position, own_face, own_level + 1u);
		}
	}
	// The per-instance tier fades out with VIEW distance: contact fidelity is only visible near
	// the camera, and the near field is the most expensive thing in the frame.
	float near_field = u_gi_trace_near_field;
	if(u_gi_near_field_fade > 0.0)
	{
		float view_distance = length(world_position - u_gi_resolve_camera.xyz);
		float fade_start = 0.5 * u_gi_near_field_fade;
		near_field *= saturate((u_gi_near_field_fade - view_distance) /
		                       max(u_gi_near_field_fade - fade_start, 1e-3));
	}
	s.near_field = near_field;
	return s;
}

/// What one gather ray measured. `radiance` is already intensity-scaled. The stage flags feed the
/// per-pixel path's diagnostics; the probe path stores only radiance and `resolved`.
struct GiRayOutcome
{
	vec3 radiance;
	/// 1 when this ray measured something the consumer should weight in -- a found entry, or an
	/// occluded miss under occlude-on-cache-miss. 0 hands its share to the environment probe.
	float resolved;
	float hit;
	float addressed;
	float found;
	float self_read;
	float t;
};

/**
 * Traces one gather ray and reads the cache at its hit: the whole per-ray pipeline in the one
 * place both gather architectures share.
 */
GiRayOutcome GiGatherRay(GiGatherSetup s, vec3 direction)
{
	GiRayOutcome o;
	o.radiance = vec3_splat(0.0);
	o.resolved = 0.0;
	o.hit = 0.0;
	o.addressed = 0.0;
	o.found = 0.0;
	o.self_read = 0.0;
	o.t = 0.0;
	// Start along the ray's OWN direction rather than pushing the origin further out along the
	// normal: a normal offset moves the shading point and lets it see past nearby geometry.
	vec3 ray_origin = s.origin + direction * (u_gi_ray_start * s.origin_voxel);
	SdfRayHit hit = SdfTraceRay(ray_origin, direction, u_gi_max_distance, s.near_field,
	                            u_gi_trace_max_steps, u_gi_trace_bias, u_gi_trace_relaxation,
	                            false);
	// Escaped the scene: contributes nothing and does NOT count, leaving the consumer's own
	// environment probe covering that fraction of the hemisphere.
	if(!hit.hit)
	{
		return o;
	}
	o.hit = 1.0;
	o.t = hit.t;
	// Same resolve the writer used: a raw hit sits short of an isosurface that is itself offset
	// from the rendered geometry, and addressing from it misses.
	SdfSurfacePoint surface = SdfResolveSurfacePoint(ray_origin + direction * hit.t);
	if(!surface.valid)
	{
		return o;
	}
	o.addressed = 1.0;
	// Same-plane rejection: a hit resolving onto the launch surface's own plane, facing the same
	// way, is the surface reading itself through the displaced isosurface -- geometrically
	// impossible on flat geometry, and it enters at full weight as near-black. Skipped without
	// counting: a failure to measure, not a measurement of darkness.
	float self_plane = dot(surface.position - s.world_position, s.world_normal);
	if(abs(self_plane) < 2.0 * s.origin_voxel && dot(surface.normal, s.world_normal) > 0.7)
	{
		o.self_read = 1.0;
		return o;
	}
	// Exact-key rejection for the cell the launch point occupies, both blend levels.
	if(s.own_key != GI_CACHE_EMPTY_KEY)
	{
		float hit_blend;
		uint hit_level = GiCacheLevelEx(surface.position, u_gi_resolve_camera.xyz, hit_blend);
		uint hit_key = GiCacheKeyForFace(surface.position, GiQuantizeNormal(surface.normal), hit_level);
		if(hit_key == s.own_key || (s.own_key_far != GI_CACHE_EMPTY_KEY && hit_key == s.own_key_far))
		{
			o.self_read = 1.0;
			return o;
		}
	}
	// Interpolated across the four cells bracketing the hit in its tangent plane -- a point
	// lookup is piecewise constant at cell scale, which is bias no filter can remove.
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
	if(!found)
	{
		// A miss on a hit means "occluded but unlit" -- counted as resolved darkness so sealed
		// rooms converge to black, but only beyond contact range, where a hit is trustworthy;
		// the contact gate keeps unpopulatable edge cells from stamping permanent black rims.
		if(u_gi_occlude_on_cache_miss && hit.t >= 2.0 * s.origin_voxel)
		{
			o.resolved = 1.0;
		}
		return o;
	}
	o.radiance = cached * u_gi_intensity;
	o.resolved = 1.0;
	o.found = 1.0;
	return o;
}

#endif // __GI_GATHER_COMMON_SH__
