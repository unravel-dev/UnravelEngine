$input v_texcoord0

/*
 * Lumen surface cache debug views (UE r.Lumen.Visualize with software ray tracing and Global Tracing). The scene
 * views march the global distance field from the camera as UE's visualize does (LumenVisualize.usf
 * VisualizeQuadsCS: expansion by the largest distance seen, step factor dithered over [0.8, 1], Lumen's 200 m
 * trace distance) and shade each hit from the cards through the object grid, exactly as the gather's rays are.
 *  mode 0, Lumen Scene (UE 3; UE's Reflection View 4 traces the same rays with software tracing): the cards' final
 *          lighting, black where no card covers the hit (UE), transparent where nothing is hit.
 *  mode 1, card atlas: the physical albedo atlas, fitted to the viewport.
 *  mode 2, coverage: the placements' mesh distance fields traced from the camera, coloured by why each hit is or
 *          is not covered - green = covered, blue = the placement has no cards, red = no resident card faces the
 *          normal, yellow = a facing card's box misses the hit, cyan = inside a box but the page is not
 *          captured, magenta = captured but the depth test fails.
 *  mode 3, Lumen Scene albedo (UE Albedo): as mode 0 with the card albedo, display encoded.
 *  mode 4, Surface Cache (UE 5): as mode 0 with pink where the object grid lists placements with cards but none
 *          covers the hit, yellow where it lists none with cards (culled from the surface cache).
 *  mode 5, object grid: the scene views' hits coloured by the furthest card stage over the placements the object
 *          grid lists at the hit, as the coverage view; grey where the grid lists none.
 *  mode 6, Lumen Scene direct lighting (UE Direct Lighting): as mode 0 with the cards' direct irradiance shown
 *          as radiance.
 *  mode 7, Lumen Scene indirect lighting (UE Indirect Lighting): as mode 6 with the radiosity irradiance.
 * The lighting views go through the lit image's tone mapping operator at the view's exposure (UE VisualizeTonemap).
 */

#include "../common.sh"
#include "../sampling.sh"
#include "../tonemapping/tonemapping.sh"
/// The global SDF's coverage (gi/sdf_clipmap.sh).
#define SDF_CLIPMAP_COVERAGE_STAGE 11
#include "gi/sdf_common.sh"

BUFFER_RO(b_lumen_scene, vec4, 9);
SAMPLER2D(s_lumen_card_direct, 5);
SAMPLER2D(s_lumen_card_indirect, 6);
SAMPLER2D(s_lumen_card_final, 7);
SAMPLER2D(s_lumen_card_albedo, 8);
SAMPLER3D(s_lumen_object_grid, 10);

#define LUMEN_SURFACE_CACHE_OBJECT_GRID
#include "lumen/lumen_surface_cache.sh"
#include "lumen/lumen_constants.sh"
#include "lumen/lumen_global_sdf.sh"

/// x = mode, y = max trace distance, z = max steps, w = trace surface bias (voxels).
uniform vec4 u_lumen_debug;
/// x = viewport aspect (width / height), y = the view's exposure for the lighting views, z = flags (bit 0: the object
/// grid colouring of the scene views' hits; bit 1: the scene views tint hits by 1 - the global SDF's coverage in
/// magenta, a diagnostic), w = the lit image's tone mapping operator (tonemapping.sh).
uniform vec4 u_lumen_debug2;
#define u_lumen_debug_show_coverage ((int(u_lumen_debug2.z) & 2) != 0)

/// SampleLumenMeshCards bias for mesh distance-field hits (20 cm).
#define LUMEN_MESH_SDF_SURFACE_CACHE_BIAS 0.2
/// SdfTraceInstances step relaxation for the coverage view (plain sphere tracing).
#define LUMEN_DEBUG_STEP_RELAXATION 1.0
/// UE's visualize march dithers its step factor between this and 1 (LumenVisualize.usf:72).
#define LUMEN_DEBUG_MIN_STEP_FACTOR 0.8

/// What the Lumen Scene views read from the cards.
#define LUMEN_SCENE_FINAL 0
#define LUMEN_SCENE_ALBEDO 1
#define LUMEN_SCENE_DIRECT 2
#define LUMEN_SCENE_INDIRECT 3

struct LumenDebugHit
{
	bool hit;
	vec3 position;
	vec3 normal;
	int instance;
	/// Voxel size of the clipmap level that answered a global distance-field hit.
	float voxel;
};

/// The camera ray through @p uv.
void LumenDebugCameraRay(vec2 uv, out vec3 origin, out vec3 direction)
{
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(0.5)));
	vec3 world_point = clipToWorld(u_invViewProj, clip);
	origin = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	direction = normalize(world_point - origin);
}

/// The placements' mesh distance fields traced from the camera (the coverage view).
LumenDebugHit LumenTraceMeshSdfView(vec2 uv)
{
	vec3 origin;
	vec3 direction;
	LumenDebugCameraRay(uv, origin, direction);
	SdfRayHit hit = SdfTraceInstances(origin, direction, 0.0, u_lumen_debug.y, int(u_lumen_debug.z),
	                                  u_lumen_debug.w, LUMEN_DEBUG_STEP_RELAXATION, true);
	LumenDebugHit result;
	result.hit = hit.hit;
	result.position = origin + direction * (hit.t + hit.hit_field);
	result.normal = dot(hit.normal, direction) > 0.0 ? -hit.normal : hit.normal;
	result.instance = hit.instance_index;
	result.voxel = 0.0;
	return result;
}

/// The global distance field traced from the camera as UE's visualize marches it.
LumenDebugHit LumenTraceGlobalSdfView(vec2 uv, vec2 pixel)
{
	vec3 origin;
	vec3 direction;
	LumenDebugCameraRay(uv, origin, direction);
	float step_factor = mix(LUMEN_DEBUG_MIN_STEP_FACTOR, 1.0, InterleavedGradientNoise(pixel));
	LumenSdfHit hit = LumenTraceGlobalSdfStepped(origin, direction, 0.0, u_lumen_debug.y, false, 0.0, 0.0, step_factor);
	LumenDebugHit result;
	result.hit = hit.hit;
	result.position = origin + direction * (hit.t + hit.hit_field);
	result.normal = dot(hit.normal, direction) > 0.0 ? -hit.normal : hit.normal;
	result.instance = -1;
	result.voxel = hit.voxel;
	return result;
}

/// Exposed radiance through the lit image's tone mapping operator.
vec4 LumenDebugToneMap(vec3 radiance)
{
	return vec4(apply_tonemapping(radiance * u_lumen_debug2.y, int(u_lumen_debug2.w), 1.0), 1.0);
}

/// The cards' value of @p source at a global distance-field hit: rgb = weighted sum, a = weight sum.
vec4 LumenSampleSceneSource(LumenDebugHit hit, int source)
{
	float voxel_extent = 0.5 * hit.voxel;
	vec4 cards = vec4_splat(0.0);
	BRANCH
	if(source == LUMEN_SCENE_ALBEDO)
	{
		cards = LumenSampleGlobalSdfHit(hit.position, hit.normal, voxel_extent, s_lumen_card_albedo);
	}
	else if(source == LUMEN_SCENE_DIRECT)
	{
		cards = LumenSampleGlobalSdfHit(hit.position, hit.normal, voxel_extent, s_lumen_card_direct);
	}
	else if(source == LUMEN_SCENE_INDIRECT)
	{
		cards = LumenSampleGlobalSdfHit(hit.position, hit.normal, voxel_extent, s_lumen_card_indirect);
	}
	else
	{
		cards = LumenSampleGlobalSdfHit(hit.position, hit.normal, voxel_extent, s_lumen_card_final);
	}
	return cards;
}

vec4 LumenSceneViewColor(LumenDebugHit hit, int source)
{
	vec4 cards = LumenSampleSceneSource(hit, source);
	if(cards.w <= 0.0)
	{
		return vec4(0.0, 0.0, 0.0, 1.0);
	}
	vec3 value = cards.xyz / cards.w;
	if(source == LUMEN_SCENE_ALBEDO)
	{
		// The atlas holds albedo in gamma 2.
		return vec4(linear_to_srgb(value * value), 1.0);
	}
	return LumenDebugToneMap(value);
}

vec4 LumenSceneView(vec2 uv, vec2 pixel, int source)
{
	LumenDebugHit hit = LumenTraceGlobalSdfView(uv, pixel);
	if(!hit.hit)
	{
		return vec4_splat(0.0);
	}
	vec4 color = LumenSceneViewColor(hit, source);
	BRANCH
	if(u_lumen_debug_show_coverage)
	{
		color.xyz = mix(color.xyz, vec3(1.0, 0.0, 1.0), 1.0 - SdfSampleClipmapCoverage(hit.position));
	}
	return color;
}

/// True when the object grid entry @p id (instance + 1, 0 = none) names a placement with cards.
bool LumenObjectGridEntryHasCards(float id)
{
	return id > 0.5 && LumenLoadInstanceCards(int(id) - 1).y > 0.5;
}

/// True when any placement the object grid lists at @p p has cards.
bool LumenObjectGridHasCards(vec3 p)
{
	vec4 ids = LumenObjectGridInstances(p);
	return LumenObjectGridEntryHasCards(ids.x) || LumenObjectGridEntryHasCards(ids.y) ||
	       LumenObjectGridEntryHasCards(ids.z) || LumenObjectGridEntryHasCards(ids.w);
}

/// Mode 0 with missing coverage marked (UE's Surface Cache view).
vec4 LumenSurfaceCacheView(vec2 uv, vec2 pixel)
{
	LumenDebugHit hit = LumenTraceGlobalSdfView(uv, pixel);
	if(!hit.hit)
	{
		return vec4_splat(0.0);
	}
	vec4 cards = LumenSampleSceneSource(hit, LUMEN_SCENE_FINAL);
	if(cards.w > 0.0)
	{
		return LumenDebugToneMap(cards.xyz / cards.w);
	}
	bool has_cards = LumenObjectGridHasCards(hit.position + hit.normal * 0.5 * hit.voxel);
	return has_cards ? vec4(1.0, 0.0, 1.0, 1.0) : vec4(1.0, 1.0, 0.0, 1.0);
}

/// The furthest stage of LumenSampleCard any card of @p instance reaches: 0 = none faces the normal,
/// 1 = facing but the box misses, 2 = in a box but the page is unmapped, 3 = depth test fails, 4 = covered.
int LumenCoverageStage(int instance, vec3 position, vec3 normal, float base_bias)
{
	vec4 cards = LumenLoadInstanceCards(instance);
	int count = int(cards.y);
	float bias = base_bias + (cards.z > 0.5 ? LUMEN_TWO_SIDED_SURFACE_CACHE_BIAS : 0.0);
	int stage = 0;
	LOOP
	for(int i = 0; i < count; ++i)
	{
		LumenCard card = LumenLoadCard(int(cards.x) + i);
		if(card.res_level.x <= 0.0 || dot(normal, card.axis_z) <= 0.0)
		{
			continue;
		}
		stage = max(stage, 1);
		vec3 d = position - card.origin;
		vec3 local = vec3(dot(d, card.axis_x), dot(d, card.axis_y), dot(d, card.axis_z));
		if(any(greaterThan(abs(local), card.extent + vec3_splat(0.5 * bias))))
		{
			continue;
		}
		stage = max(stage, 2);
		local.xy = clamp(local.xy, -card.extent.xy, card.extent.xy);
		LumenCardSample s = LumenComputeCardSample(card, local.xy);
		if(!s.valid)
		{
			continue;
		}
		stage = max(stage, 3);
		vec4 weighted = LumenSampleCard(int(cards.x) + i, position, normal, bias, s_lumen_card_albedo);
		if(weighted.w > 0.0)
		{
			stage = 4;
		}
	}
	return stage;
}

vec4 LumenCoverageColor(int stage)
{
	if(stage == 4)
	{
		return vec4(0.0, 1.0, 0.0, 1.0);
	}
	if(stage == 3)
	{
		return vec4(1.0, 0.0, 1.0, 1.0);
	}
	if(stage == 2)
	{
		return vec4(0.0, 1.0, 1.0, 1.0);
	}
	if(stage == 1)
	{
		return vec4(1.0, 1.0, 0.0, 1.0);
	}
	return vec4(1.0, 0.0, 0.0, 1.0);
}

/// Why each mesh distance-field hit is or is not covered by its placement's cards (see mode 2).
vec4 LumenCoverageView(vec2 uv)
{
	LumenDebugHit hit = LumenTraceMeshSdfView(uv);
	if(!hit.hit)
	{
		return vec4_splat(0.0);
	}
	if(hit.instance < 0 || float(hit.instance) >= u_lumen_surface_cache.w)
	{
		return vec4(0.3, 0.3, 0.3, 1.0);
	}
	if(LumenLoadInstanceCards(hit.instance).y < 0.5)
	{
		return vec4(0.0, 0.0, 1.0, 1.0);
	}
	return LumenCoverageColor(LumenCoverageStage(hit.instance, hit.position, hit.normal, LUMEN_MESH_SDF_SURFACE_CACHE_BIAS));
}

/// The furthest card stage over the placements the object grid lists at each global distance-field hit (mode 5).
vec4 LumenObjectGridView(vec2 uv, vec2 pixel)
{
	LumenDebugHit hit = LumenTraceGlobalSdfView(uv, pixel);
	if(!hit.hit)
	{
		return vec4_splat(0.0);
	}
	vec4 ids = LumenObjectGridInstances(hit.position + hit.normal * 0.5 * hit.voxel);
	if(ids.x < 0.5)
	{
		return vec4(0.3, 0.3, 0.3, 1.0);
	}
	float bias = LUMEN_GLOBAL_SDF_SURFACE_CACHE_BIAS * u_lumen_object_grid_params.y * 0.5 * hit.voxel;
	int stage = LumenCoverageStage(int(ids.x) - 1, hit.position, hit.normal, bias);
	if(ids.y > 0.5)
	{
		stage = max(stage, LumenCoverageStage(int(ids.y) - 1, hit.position, hit.normal, bias));
	}
	if(ids.z > 0.5)
	{
		stage = max(stage, LumenCoverageStage(int(ids.z) - 1, hit.position, hit.normal, bias));
	}
	if(ids.w > 0.5)
	{
		stage = max(stage, LumenCoverageStage(int(ids.w) - 1, hit.position, hit.normal, bias));
	}
	return LumenCoverageColor(stage);
}

vec4 LumenAtlasView(vec2 uv)
{
	// Square atlas centred in the viewport.
	float aspect = u_lumen_debug2.x;
	vec2 atlas_uv = aspect >= 1.0 ? vec2((uv.x - 0.5) * aspect + 0.5, uv.y) : vec2(uv.x, (uv.y - 0.5) / aspect + 0.5);
	if(any(lessThan(atlas_uv, vec2_splat(0.0))) || any(greaterThan(atlas_uv, vec2_splat(1.0))))
	{
		return vec4(0.0, 0.0, 0.0, 1.0);
	}
	ivec2 size = textureSize(s_lumen_card_albedo, 0);
	ivec2 texel = min(ivec2(atlas_uv * vec2(size)), size - ivec2(1, 1));
	vec4 albedo = texelFetch(s_lumen_card_albedo, texel, 0);
	return vec4(albedo.xyz * albedo.xyz, 1.0);
}

void main()
{
	vec2 uv = v_texcoord0;
	vec2 pixel = gl_FragCoord.xy;
	float mode = u_lumen_debug.x;
	// Explicit branches: fxc evaluates both sides of ?:, and the scene views trace.
	BRANCH
	if(mode < 0.5)
	{
		gl_FragColor = LumenSceneView(uv, pixel, LUMEN_SCENE_FINAL);
	}
	else if(mode < 1.5)
	{
		gl_FragColor = LumenAtlasView(uv);
	}
	else if(mode < 2.5)
	{
		gl_FragColor = LumenCoverageView(uv);
	}
	else if(mode < 3.5)
	{
		gl_FragColor = LumenSceneView(uv, pixel, LUMEN_SCENE_ALBEDO);
	}
	else if(mode < 4.5)
	{
		gl_FragColor = LumenSurfaceCacheView(uv, pixel);
	}
	else if(mode < 5.5)
	{
		gl_FragColor = LumenObjectGridView(uv, pixel);
	}
	else if(mode < 6.5)
	{
		gl_FragColor = LumenSceneView(uv, pixel, LUMEN_SCENE_DIRECT);
	}
	else
	{
		gl_FragColor = LumenSceneView(uv, pixel, LUMEN_SCENE_INDIRECT);
	}
}
