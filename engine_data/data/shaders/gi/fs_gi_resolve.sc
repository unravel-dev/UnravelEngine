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

/// x = ray count, y = max trace distance, z = normal bias in world units, w = frame index.
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
	// Lift off the surface before tracing. A ray starting exactly on the isosurface reads a
	// distance of zero and terminates immediately, reporting the origin as its own occluder.
	vec3 origin = world_position + world_normal * u_gi_normal_bias;
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
		                            u_gi_trace_max_steps, u_gi_trace_bias, u_gi_trace_relaxation);
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
		uint level = GiCacheLevel(surface.position, u_gi_resolve_camera.xyz);
		uint slot = GiCacheFindSurface(surface.position, surface.normal, level);
		// A miss contributes NOTHING rather than black. It means this cell has not been lit yet,
		// not that it is dark, and averaging in a zero would darken exactly the places the cache
		// has yet to reach. Leaving it out of both sums lowers the weight instead, so the
		// consumer falls back toward its environment probe there -- the conservative direction.
		if(slot == GI_CACHE_INVALID_SLOT)
		{
			continue;
		}
		sum += b_gi_cache_data[GiCacheDataIndex(slot, GI_CACHE_DATA_RADIANCE)].xyz * u_gi_intensity;
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
