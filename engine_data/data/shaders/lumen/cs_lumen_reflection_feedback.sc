/*
 * GI reflections, the surface cache feedback: the card pages reflection hits sample and the resolution they want.
 * Runs after the distance-field stage: one trace texel per LUMEN_FEEDBACK_TILE x LUMEN_FEEDBACK_TILE tile, at this
 * frame's jitter in the tile, takes the stage's hit where it hit the distance field (its radiance alpha): the point at
 * its hit distance and the field's gradient there. The stage itself has no bgfx stage left to bind the table to, and
 * a march of its own here would cost as much as a tenth of the stage. At the hit, the card sample of largest weight
 * asks for the res level its ray cone wants
 * (log2(the card's half extent / max(the cone's radius at the hit, 1 cm)) - 0.5, levels 3 to 11) and the page under
 * its card UV at that level (on the page grid without the card's aspect bias, 2^(level - 7) pages across, which
 * lumen_scene maps into the card's mip).
 *
 * The element (card | level << 20 | page x << 24 | page y << 28; never 0, the level is at least 3) is counted in a
 * linear-probing hash table, keys and counts, that persists over a feedback window:
 * lumen_surface_cache_pass reads it back and clears it.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#define LUMEN_REFLECTION_TILES_STAGE 12
#include "lumen/lumen_reflection_common.sh"
/// The global SDF's coverage (gi/sdf_clipmap.sh).
#define SDF_CLIPMAP_COVERAGE_STAGE 10
#define SDF_CLIPMAP_MIP_STAGE 9
#include "lumen/lumen_global_sdf.sh"

SAMPLER2D(s_lumen_reflection_ray, 0);
/// The distance-field stage's encoded hit distance and radiance (a = 1 at a distance-field hit).
SAMPLER2D(s_lumen_reflection_hit, 1);
SAMPLER2D(s_lumen_depth, 2);
SAMPLER2D(s_lumen_reflection_radiance, 6);
BUFFER_RW(b_lumen_feedback_keys, uint, 3);
BUFFER_RW(b_lumen_feedback_counts, uint, 5);

/// The surface cache (lumen_surface_cache.sh), read through the object grid: the cards a hit would sample.
BUFFER_RO(b_lumen_scene, vec4, 13);
SAMPLER2D(s_lumen_card_final, 14);
SAMPLER3D(s_lumen_object_grid, 15);
#define LUMEN_SURFACE_CACHE_OBJECT_GRID
#include "lumen/lumen_surface_cache.sh"

/// xy = this frame's texel in each feedback tile, z = the hash table's index mask (its size - 1).
uniform vec4 u_lumen_feedback;

/// Trace texels per feedback tile side; one texel per tile feeds back each frame.
#define LUMEN_FEEDBACK_TILE 16
/// The bias added to the res level a ray cone wants, the range of res levels feedback asks for, and the 1 cm floor of
/// the cone's radius.
#define LUMEN_FEEDBACK_RES_LEVEL_BIAS -0.5
#define LUMEN_FEEDBACK_MIN_RES_LEVEL 3.0
#define LUMEN_FEEDBACK_MAX_RES_LEVEL 11.0
#define LUMEN_FEEDBACK_MIN_SAMPLE_RADIUS 0.01
/// The largest cone angle tan() is taken at, short of a right angle: past it tan() wraps, and the wrapped radius
/// would ask for the finest level.
#define LUMEN_FEEDBACK_MAX_CONE_ANGLE 1.5
/// The hash table's longest linear probe.
#define LUMEN_FEEDBACK_MAX_PROBES 32
/// The card indices an element holds (20 bits).
#define LUMEN_FEEDBACK_MAX_CARDS 1048576
/// The highest res level whose card mip fits in one page: above it a mip has 2^(level - 7) pages across.
#define LUMEN_FEEDBACK_SUB_ALLOCATION_RES_LEVEL 7.0

/// The Murmur3 32-bit finalizer: spreads every bit of the element over the whole key.
uint LumenMurmurMix(uint hash)
{
	hash ^= hash >> 16u;
	hash *= 0x85ebca6bu;
	hash ^= hash >> 13u;
	hash *= 0xc2b2ae35u;
	hash ^= hash >> 16u;
	return hash;
}

/// The card of largest weight among the cards of instance @p id - 1 at a hit, folded into @p best (x = card + 1, 0 for
/// none; y = its weight; zw = its card UV) and @p best_extent (its largest half extent across its face).
vec4 LumenFeedbackInstance(vec4 best, inout float best_extent, float id, vec3 position, vec3 normal, float bias)
{
	BRANCH
	if(id < 0.5)
	{
		return best;
	}
	vec4 cards = LumenLoadInstanceCards(int(id) - 1);
	int first = int(cards.x);
	int count = int(cards.y);
	float sample_bias = bias + (cards.z > 0.5 ? LUMEN_TWO_SIDED_SURFACE_CACHE_BIAS : 0.0);
	LOOP
	for(int i = 0; i < count; ++i)
	{
		LumenCardHit hit = LumenEvaluateCardHit(first + i, position, normal, sample_bias);
		if(hit.sample_weight > best.y)
		{
			best = vec4(float(first + i + 1), hit.sample_weight, hit.s.card_uv);
			best_extent = hit.face_extent;
		}
	}
	return best;
}

/// Counts @p element in the hash table: finds or claims its key by linear probing, then adds 1 to its count.
void LumenAddFeedback(uint element)
{
	uint mask = uint(u_lumen_feedback.z);
	uint key = LumenMurmurMix(element);
	LOOP
	for(int probe = 0; probe < LUMEN_FEEDBACK_MAX_PROBES; ++probe)
	{
		uint slot = (key + uint(probe)) & mask;
		uint stored = 0u;
		atomicFetchCompareExchange(b_lumen_feedback_keys[slot], 0u, element, stored);
		if(stored == 0u || stored == element)
		{
			uint previous = 0u;
			atomicFetchAndAdd(b_lumen_feedback_counts[slot], 1u, previous);
			return;
		}
	}
}

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 trace_coord = ivec2(gl_GlobalInvocationID.xy) * LUMEN_FEEDBACK_TILE + ivec2(u_lumen_feedback.xy);
	if(any(greaterThanEqual(trace_coord, LumenReflectionTraceSize())))
	{
		return;
	}
	vec4 ray = LumenReflectionTraceRay(s_lumen_reflection_ray, trace_coord);
	if(ray.w <= 0.0 || texelFetch(s_lumen_reflection_radiance, trace_coord, 0).w < 0.5)
	{
		return;
	}
	// The distance-field stage's ray (cs_lumen_reflection_world.sc) and its hit.
	ivec2 pixel = LumenReflectionTracePixel(trace_coord);
	vec3 direction = normalize(ray.xyz);
	float depth01 = texelFetch(s_lumen_depth, pixel, 0).x;
	vec3 position = LumenWorldFromDepth(LumenPixelUv(pixel), depth01);
	vec3 origin = position + LUMEN_SURFACE_BIAS * direction;
	float hit_t = abs(texelFetch(s_lumen_reflection_hit, trace_coord, 0).x);
	vec3 surface = origin + direction * hit_t;
	float voxel = SdfSampleClipmapLevels(surface).voxel_size;
	vec3 normal = LumenGlobalSdfNormal(surface, voxel, -direction);
	vec3 surface_normal = dot(normal, direction) > 0.0 ? -normal : normal;
	// The cards the hit samples (LumenSampleGlobalSdfHit), the one of largest weight kept.
	float voxel_extent = 0.5 * voxel;
	vec4 ids = LumenObjectGridInstances(surface + surface_normal * voxel_extent);
	float bias = LUMEN_GLOBAL_SDF_SURFACE_CACHE_BIAS * u_lumen_object_grid_params.y * voxel_extent;
	vec4 best = vec4_splat(0.0);
	float best_extent = 0.0;
	best = LumenFeedbackInstance(best, best_extent, ids.x, surface, surface_normal, bias);
	best = LumenFeedbackInstance(best, best_extent, ids.y, surface, surface_normal, bias);
	best = LumenFeedbackInstance(best, best_extent, ids.z, surface, surface_normal, bias);
	best = LumenFeedbackInstance(best, best_extent, ids.w, surface, surface_normal, bias);
	if(best.x < 0.5 || best.x > float(LUMEN_FEEDBACK_MAX_CARDS))
	{
		return;
	}
	// The ray cone: the eye-to-pixel spread plus the ray's cone angle, its radius at the hit.
	float pixel_spread = atan(2.0 / (u_proj[1][1] * u_lumen_view_size.y));
	float cone = min(pixel_spread + ray.w, LUMEN_FEEDBACK_MAX_CONE_ANGLE);
	float sample_radius = max(tan(cone) * hit_t, LUMEN_FEEDBACK_MIN_SAMPLE_RADIUS);
	float level = clamp(log2(best_extent / sample_radius) + LUMEN_FEEDBACK_RES_LEVEL_BIAS, LUMEN_FEEDBACK_MIN_RES_LEVEL,
	                    LUMEN_FEEDBACK_MAX_RES_LEVEL);
	float grid = exp2(max(floor(level) - LUMEN_FEEDBACK_SUB_ALLOCATION_RES_LEVEL, 0.0));
	uvec2 page = uvec2(min(best.zw * grid, vec2_splat(grid - 1.0)));
	uint element = uint(best.x - 0.5) | (uint(level) << 20u) | (page.x << 24u) | (page.y << 28u);
	LumenAddFeedback(element);
}
