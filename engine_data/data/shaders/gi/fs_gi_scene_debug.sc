$input v_texcoord0

/*
 * Surface cache debug views: the scene views, traced through the global distance field in software, and the card
 * atlas, card coverage and object grid views.
 * The scene views march the global distance field from the camera (expansion by the largest distance seen, step
 * factor dithered over [0.8, 1]) and shade each hit from the cards through the object grid, exactly as the gather's
 * rays are; a ray that hits nothing reads the sky: the environment's radiance SH our rays read.
 * Modes (the debug pass id - 42):
 *  0  GI Scene: the cards' final lighting, black where no card covers the hit.
 *  1  card atlas: the physical albedo atlas, fitted to the viewport.
 *  2  coverage: the placements' mesh distance fields traced from the camera, coloured by why each hit is or is not
 *     covered - green = covered, blue = the placement has no cards, red = no resident card faces the normal, yellow =
 *     a facing card's box misses the hit, cyan = inside a box but the page is not captured, magenta = captured but
 *     the depth test fails.
 *  3  Albedo: the cards' albedo.
 *  4  Surface Cache: as 0 with pink where the object grid lists placements with cards but none covers the hit,
 *     yellow where it lists none with cards (culled from the surface cache).
 *  5  object grid: the scene views' hits coloured by the furthest card stage over the placements the object grid
 *     lists at the hit, as the coverage view; grey where the grid lists none.
 *  6  Direct Lighting, 7 Indirect Lighting: the cards' direct irradiance and radiosity, shown as radiance.
 *  9  Reflection View: as 0, traced as far as the reflections trace.
 *  10 Geometry Normals: the hit's distance field normal.
 *  11 Normals: the cards' normals.
 *  12 Emissive: the cards' emissive.
 *  13 Card Weights: a colour per card, darkened on the lines through its 8x8 atlas tiles' centres.
 *  14 Direct Lighting Updates, 15 Indirect Lighting Updates: white where the page was lit this frame, red
 *     to blue over the 8 (16) frames since.
 *  16 Radiosity Frames Accumulated: each 8x8 tile's radiosity update count over its steady value inside its tile
 *     outline, pink and yellow as 4.
 * The values reach the screen with the albedo display encoded, the normals as they are, everything else through
 * the lit image's tone mapping operator - the lighting at the view's exposure, the debug colours at an exposure
 * of 1, so they read the same whatever the view's exposure.
 * In a tile (u_gi_debug3.xy > 0: an overview's tile) the view fills the tile, whose corners are rounded.
 */

#include "../common.sh"
#include "../sampling.sh"
#include "../lighting.sh"
#include "../tonemapping/tonemapping.sh"
/// The global SDF's coverage (gi/sdf_clipmap.sh).
#define SDF_CLIPMAP_COVERAGE_STAGE 11
#include "gi/sdf_common.sh"

BUFFER_RO(b_gi_scene, vec4, 9);
SAMPLER2D(s_gi_card_direct, 5);
SAMPLER2D(s_gi_card_indirect, 6);
SAMPLER2D(s_gi_card_final, 7);
SAMPLER2D(s_gi_card_albedo, 8);
SAMPLER3D(s_gi_object_grid, 10);
/// What modes 11, 12 and 16 read per card sample: the normal atlas, the emissive atlas, or the radiosity update
/// count per 8x8 atlas tile.
SAMPLER2D(s_gi_debug_values, 13);
/// Per page-table entry (gi_scene::get_page_lighting_ages): x = frames since its direct lighting update, y = since
/// its indirect lighting update.
BUFFER_RO(b_gi_page_ages, vec4, 14);
/// The environment's radiance SH (texels 0-8, absolute radiance); texel 9 holds the GPU pre-exposure, read there for
/// want of a free stage.
SAMPLER2D(s_gi_env_sh, 15);
#define PRE_EXPOSURE_ENVIRONMENT s_gi_env_sh
#include "../pre_exposure.sh"
/// The view's pre-exposure the debug views scale radiance by: the CPU's share and the GPU's.
#define u_gi_debug_exposure (u_gi_debug2.y * u_pre_exposure_gpu_scale)

#define GI_SURFACE_CACHE_OBJECT_GRID
#include "gi/gi_surface_cache.sh"
#include "gi/gi_surface_cache_lighting.sh"
#include "gi/gi_pass_constants.sh"
#include "gi/gi_global_sdf.sh"
#include "gi/gi_visualize.sh"

/// x = mode, y = max trace distance, z = max steps, w = trace surface bias (voxels).
uniform vec4 u_gi_debug;
/// x = viewport aspect (width / height), y = the view's exposure for the lighting views, z = flags (bit 0: the object
/// grid colouring of the scene views' hits; bit 1: the scene views tint hits by 1 - the global SDF's coverage in
/// magenta, a diagnostic), w = the lit image's tone mapping operator (tonemapping.sh).
uniform vec4 u_gi_debug2;
/// xy = the tile's size in pixels (0 = the whole view), zw = the whole view's size in pixels.
uniform vec4 u_gi_debug3;
#define u_gi_debug_show_coverage ((int(u_gi_debug2.z) & 2) != 0)

/// The card sampling bias for mesh distance-field hits (20 cm).
#define GI_MESH_SDF_SURFACE_CACHE_BIAS 0.2
/// SdfTraceInstances step relaxation for the coverage view (plain sphere tracing).
#define GI_DEBUG_STEP_RELAXATION 1.0
/// The scene views' march dithers its step factor between this and 1.
#define GI_DEBUG_MIN_STEP_FACTOR 0.8
/// The lighting updates views reach blue this many frames after a page's update.
#define GI_DEBUG_DIRECT_UPDATES_SCALE 8.0
#define GI_DEBUG_INDIRECT_UPDATES_SCALE 16.0
/// The radiosity frames view shows the update count over this: the count's steady value (cs_gi_radiosity_integrate.sc
/// GI_RADIOSITY_ACCUMULATED_UPDATES), white once a tile has settled.
#define GI_DEBUG_RADIOSITY_FRAMES_SCALE 5.0
/// The card views' atlas grid: one cell per 8x8 tile of the lighting.
#define GI_DEBUG_CARD_TILE_SIZE 8.0

#define GI_DEBUG_SCENE 0
#define GI_DEBUG_ATLAS 1
#define GI_DEBUG_COVERAGE 2
#define GI_DEBUG_ALBEDO 3
#define GI_DEBUG_SURFACE_CACHE 4
#define GI_DEBUG_OBJECT_GRID 5
#define GI_DEBUG_DIRECT 6
#define GI_DEBUG_INDIRECT 7
#define GI_DEBUG_REFLECTION_VIEW 9
#define GI_DEBUG_GEOMETRY_NORMALS 10
#define GI_DEBUG_NORMALS 11
#define GI_DEBUG_EMISSIVE 12
#define GI_DEBUG_CARD_WEIGHTS 13
#define GI_DEBUG_DIRECT_UPDATES 14
#define GI_DEBUG_INDIRECT_UPDATES 15
#define GI_DEBUG_RADIOSITY_FRAMES 16

struct GiDebugHit
{
	bool hit;
	vec3 position;
	vec3 normal;
	int instance;
	/// Voxel size of the clipmap level that answered a global distance-field hit.
	float voxel;
};

/// The camera ray through @p uv.
void GiDebugCameraRay(vec2 uv, out vec3 origin, out vec3 direction)
{
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(0.5)));
	vec3 world_point = clipToWorld(u_invViewProj, clip);
	origin = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	direction = normalize(world_point - origin);
}

/// The placements' mesh distance fields traced from the camera (the coverage view).
GiDebugHit GiTraceMeshSdfView(vec2 uv)
{
	vec3 origin;
	vec3 direction;
	GiDebugCameraRay(uv, origin, direction);
	SdfRayHit hit = SdfTraceInstances(origin, direction, 0.0, u_gi_debug.y, int(u_gi_debug.z),
	                                  u_gi_debug.w, GI_DEBUG_STEP_RELAXATION, true);
	GiDebugHit result;
	result.hit = hit.hit;
	result.position = origin + direction * (hit.t + hit.hit_field);
	result.normal = dot(hit.normal, direction) > 0.0 ? -hit.normal : hit.normal;
	result.instance = hit.instance_index;
	result.voxel = 0.0;
	return result;
}

/// The global distance field traced from the camera, the step factor dithered per pixel.
GiDebugHit GiTraceGlobalSdfView(vec2 uv, vec2 pixel)
{
	vec3 origin;
	vec3 direction;
	GiDebugCameraRay(uv, origin, direction);
	float step_factor = mix(GI_DEBUG_MIN_STEP_FACTOR, 1.0, InterleavedGradientNoise(pixel));
	GiSdfHit hit = GiTraceGlobalSdfStepped(origin, direction, 0.0, u_gi_debug.y, false, 0.0, 0.0, step_factor);
	GiDebugHit result;
	result.hit = hit.hit;
	result.position = origin + direction * (hit.t + hit.hit_field);
	result.normal = dot(hit.normal, direction) > 0.0 ? -hit.normal : hit.normal;
	result.instance = -1;
	result.voxel = hit.voxel;
	return result;
}

/// Exposed radiance through the lit image's tone mapping operator.
vec4 GiDebugToneMap(vec3 radiance)
{
	return vec4(apply_tonemapping(radiance * u_gi_debug_exposure, int(u_gi_debug2.w), 1.0), 1.0);
}

/// A pre-exposed value as @p mode shows it: albedo display encoded, normals as they are, the rest tone mapped.
vec4 GiDebugFinalize(vec3 value, int mode)
{
	if(mode == GI_DEBUG_ALBEDO)
	{
		return vec4(linear_to_srgb(saturate(value)), 1.0);
	}
	if(mode == GI_DEBUG_NORMALS || mode == GI_DEBUG_GEOMETRY_NORMALS)
	{
		return vec4(value, 1.0);
	}
	return vec4(apply_tonemapping(value, int(u_gi_debug2.w), 1.0), 1.0);
}

/// What a camera ray that hits nothing shows in @p mode: the sky's radiance SH, then the mode's finalize.
vec4 GiDebugSky(vec2 uv, int mode)
{
	vec3 origin;
	vec3 direction;
	GiDebugCameraRay(uv, origin, direction);
	return GiDebugFinalize(eval_radiance_sh(s_gi_env_sh, direction) * u_gi_debug_exposure, mode);
}

/// How one card covers a hit, as GiSampleCard computes it before it reads the atlases (kept
/// apart so the production sampler compiles as it would without the debug views): the footprint, its texel weights
/// normalized over the texels that pass the depth test, and the sample's weight - 0 when the card does not cover the
/// hit.
struct GiCardTap
{
	GiCardSample s;
	vec4 weights;
	float weight;
};

GiCardTap GiSampleCardTap(int card_index, vec3 position, vec3 normal, float bias)
{
	GiCardTap tap;
	tap.s.texel = ivec2(0, 0);
	tap.s.weights = vec4_splat(0.0);
	tap.s.atlas_coord = vec2_splat(0.0);
	tap.s.page_index = 0;
	tap.s.valid = false;
	tap.weights = vec4_splat(0.0);
	tap.weight = 0.0;
	GiCard card = GiLoadCard(card_index);
	float facing = dot(normal, card.axis_z);
	vec3 d = position - card.origin;
	vec3 local = vec3(dot(d, card.axis_x), dot(d, card.axis_y), dot(d, card.axis_z));
	if(card.res_level.x <= 0.0 || facing <= 0.0 || any(greaterThan(abs(local), card.extent + vec3_splat(0.5 * bias))))
	{
		return tap;
	}
	local.xy = clamp(local.xy, -card.extent.xy, card.extent.xy);
	GiCardSample s = GiComputeCardSample(card, local.xy);
	if(!s.valid)
	{
		return tap;
	}
	float hit_depth = -(local.z / card.extent.z) * 0.5 + 0.5;
	float threshold = bias / card.extent.z;
	float falloff = 0.25 * threshold;
	vec4 visibility = vec4(GiCardTexelVisibility(texelFetch(s_gi_card_final, s.texel, 0).w, hit_depth, threshold, falloff),
	                       GiCardTexelVisibility(texelFetch(s_gi_card_final, s.texel + ivec2(1, 0), 0).w, hit_depth, threshold, falloff),
	                       GiCardTexelVisibility(texelFetch(s_gi_card_final, s.texel + ivec2(0, 1), 0).w, hit_depth, threshold, falloff),
	                       GiCardTexelVisibility(texelFetch(s_gi_card_final, s.texel + ivec2(1, 1), 0).w, hit_depth, threshold, falloff));
	vec4 weights = s.weights * visibility;
	float weight_sum = dot(weights, vec4_splat(1.0));
	float sample_weight = facing * facing * weight_sum;
	if(sample_weight <= 0.0)
	{
		return tap;
	}
	tap.s = s;
	tap.weights = weights / weight_sum;
	tap.weight = sample_weight;
	return tap;
}

/// The Card Weights colour of card @p card_index at the atlas position @p atlas_coord (texels).
vec3 GiCardWeightsColor(int card_index, vec2 atlas_coord)
{
	float index = float(card_index);
	vec3 color;
	color.x = mod(index, 4.0) / 3.0;
	color.y = mod(floor(index / 4.0), 4.0) / 3.0;
	color.z = saturate(1.0 - color.x - color.y);
	// HLSL smoothstep(|grid - 0.5|, 0, 0.01), written out: GLSL leaves a reversed edge pair undefined.
	vec2 offset = abs(fract(atlas_coord / GI_DEBUG_CARD_TILE_SIZE) - 0.5);
	vec2 t = vec2(offset.x > 0.0 ? saturate((offset.x - 0.01) / offset.x) : 1.0,
	              offset.y > 0.0 ? saturate((offset.y - 0.01) / offset.y) : 1.0);
	vec2 grid = t * t * (3.0 - 2.0 * t);
	return color * mix(0.25, 1.0, saturate(grid.x * grid.y));
}

/// 1 inside an 8x8 atlas tile, 0 on its outline (the radiosity frames view's grid).
float GiCardTileOutline(vec2 atlas_coord)
{
	vec2 grid = abs(fract(atlas_coord / GI_DEBUG_CARD_TILE_SIZE) * 2.0 - 1.0);
	return (grid.x > 0.98 ? 0.0 : 1.0) * (grid.y > 0.98 ? 0.0 : 1.0);
}

/// True for the modes that read each card sample through GiCardDebugValue.
bool GiIsCardDebugMode(int mode)
{
	return mode == GI_DEBUG_ALBEDO || mode >= GI_DEBUG_NORMALS;
}

/// The pre-exposed value one card sample contributes in @p mode.
vec3 GiCardDebugValue(int mode, int card_index, GiCardTap tap)
{
	BRANCH
	if(mode == GI_DEBUG_ALBEDO)
	{
		// The atlas holds albedo in gamma 2: each card's sample is squared back to linear before the cards blend.
		vec3 encoded = GiFetchCardAtlas(s_gi_card_albedo, tap.s, tap.weights);
		return encoded * encoded;
	}
	else if(mode == GI_DEBUG_NORMALS)
	{
		// The atlas holds card-space normals; each texel is turned into world space before the blend.
		GiCard card = GiLoadCard(card_index);
		vec3 normal =
		    GiDecodeCardNormal(texelFetch(s_gi_debug_values, tap.s.texel, 0).xy, card.axis_x, card.axis_y, card.axis_z) *
		        tap.weights.x +
		    GiDecodeCardNormal(texelFetch(s_gi_debug_values, tap.s.texel + ivec2(1, 0), 0).xy, card.axis_x, card.axis_y,
		                          card.axis_z) *
		        tap.weights.y +
		    GiDecodeCardNormal(texelFetch(s_gi_debug_values, tap.s.texel + ivec2(0, 1), 0).xy, card.axis_x, card.axis_y,
		                          card.axis_z) *
		        tap.weights.z +
		    GiDecodeCardNormal(texelFetch(s_gi_debug_values, tap.s.texel + ivec2(1, 1), 0).xy, card.axis_x, card.axis_y,
		                          card.axis_z) *
		        tap.weights.w;
		return normal / max(length(normal), 1e-6) * 0.5 + 0.5;
	}
	else if(mode == GI_DEBUG_EMISSIVE)
	{
		return GiFetchCardAtlas(s_gi_debug_values, tap.s, tap.weights) * u_gi_debug_exposure;
	}
	else if(mode == GI_DEBUG_CARD_WEIGHTS)
	{
		return GiCardWeightsColor(card_index, tap.s.atlas_coord);
	}
	else if(mode == GI_DEBUG_DIRECT_UPDATES || mode == GI_DEBUG_INDIRECT_UPDATES)
	{
		vec4 ages = b_gi_page_ages[tap.s.page_index - int(u_gi_surface_cache.y)];
		bool is_direct = mode == GI_DEBUG_DIRECT_UPDATES;
		float frames = is_direct ? ages.x : ages.y;
		float scale = is_direct ? GI_DEBUG_DIRECT_UPDATES_SCALE : GI_DEBUG_INDIRECT_UPDATES_SCALE;
		return frames < 1.0 ? vec3_splat(1.0) : mix(vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), saturate(frames / scale));
	}
	ivec2 tile = ivec2(floor(tap.s.atlas_coord / GI_DEBUG_CARD_TILE_SIZE));
	float frames = texelFetch(s_gi_debug_values, tile, 0).x;
	return vec3_splat(frames / GI_DEBUG_RADIOSITY_FRAMES_SCALE * GiCardTileOutline(tap.s.atlas_coord));
}

/// GiSampleInstanceCards with each card's contribution from GiCardDebugValue.
vec4 GiSampleInstanceCardsDebug(int instance, vec3 position, vec3 normal, float bias, int mode)
{
	vec4 accumulated = vec4_splat(0.0);
	vec4 cards = GiLoadInstanceCards(instance);
	int first = int(cards.x);
	int count = int(cards.y);
	float sample_bias = bias + (cards.z > 0.5 ? GI_TWO_SIDED_SURFACE_CACHE_BIAS : 0.0);
	LOOP
	for(int i = 0; i < count; ++i)
	{
		GiCardTap tap = GiSampleCardTap(first + i, position, normal, sample_bias);
		BRANCH
		if(tap.weight > 0.0)
		{
			accumulated += vec4(GiCardDebugValue(mode, first + i, tap) * tap.weight, tap.weight);
		}
	}
	return accumulated;
}

vec4 GiAccumulateObjectDebug(vec4 accumulated, float id, vec3 position, vec3 normal, float bias, int mode)
{
	BRANCH
	if(id > 0.5 && accumulated.w < GI_OBJECT_GRID_WEIGHT_DONE)
	{
		accumulated += GiSampleInstanceCardsDebug(int(id) - 1, position, normal, bias, mode);
	}
	return accumulated;
}

/// GiSampleGlobalSdfHit with each card's contribution from GiCardDebugValue.
vec4 GiSampleGlobalSdfHitDebug(vec3 position, vec3 normal, float voxel_extent, int mode)
{
	vec4 ids = GiObjectGridInstances(position + normal * voxel_extent);
	float bias = GI_GLOBAL_SDF_SURFACE_CACHE_BIAS * u_gi_object_grid_params.y * voxel_extent;
	vec4 accumulated = vec4_splat(0.0);
	accumulated = GiAccumulateObjectDebug(accumulated, ids.x, position, normal, bias, mode);
	accumulated = GiAccumulateObjectDebug(accumulated, ids.y, position, normal, bias, mode);
	accumulated = GiAccumulateObjectDebug(accumulated, ids.z, position, normal, bias, mode);
	accumulated = GiAccumulateObjectDebug(accumulated, ids.w, position, normal, bias, mode);
	return accumulated;
}

/// True when the object grid entry @p id (instance + 1, 0 = none) names a placement with cards.
bool GiObjectGridEntryHasCards(float id)
{
	return id > 0.5 && GiLoadInstanceCards(int(id) - 1).y > 0.5;
}

/// True when any placement the object grid lists at @p p has cards.
bool GiObjectGridHasCards(vec3 p)
{
	vec4 ids = GiObjectGridInstances(p);
	return GiObjectGridEntryHasCards(ids.x) || GiObjectGridEntryHasCards(ids.y) ||
	       GiObjectGridEntryHasCards(ids.z) || GiObjectGridEntryHasCards(ids.w);
}

/// The cards' value at a global distance-field hit in @p mode: rgb = weighted pre-exposed sum, a = weight sum.
vec4 GiSampleSceneHit(GiDebugHit hit, int mode)
{
	float voxel_extent = 0.5 * hit.voxel;
	vec4 cards = vec4_splat(0.0);
	BRANCH
	if(GiIsCardDebugMode(mode))
	{
		cards = GiSampleGlobalSdfHitDebug(hit.position, hit.normal, voxel_extent, mode);
	}
	else if(mode == GI_DEBUG_DIRECT)
	{
		cards = GiSampleGlobalSdfHit(hit.position, hit.normal, voxel_extent, s_gi_card_direct);
		cards.xyz *= u_gi_debug_exposure;
	}
	else if(mode == GI_DEBUG_INDIRECT)
	{
		cards = GiSampleGlobalSdfHit(hit.position, hit.normal, voxel_extent, s_gi_card_indirect);
		cards.xyz *= u_gi_debug_exposure;
	}
	else
	{
		cards = GiSampleGlobalSdfHit(hit.position, hit.normal, voxel_extent, s_gi_card_final);
		cards.xyz *= u_gi_debug_exposure;
	}
	return cards;
}

/// The scene views (every mode but 1, 2 and 5).
vec4 GiSceneView(vec2 uv, vec2 pixel, int mode)
{
	GiDebugHit hit = GiTraceGlobalSdfView(uv, pixel);
	if(!hit.hit)
	{
		return GiDebugSky(uv, mode);
	}
	vec3 value = vec3_splat(0.0);
	BRANCH
	if(mode == GI_DEBUG_GEOMETRY_NORMALS)
	{
		value = hit.normal * 0.5 + 0.5;
	}
	else
	{
		vec4 cards = GiSampleSceneHit(hit, mode);
		if(cards.w > 0.0)
		{
			value = cards.xyz / cards.w;
		}
		else if(mode == GI_DEBUG_SURFACE_CACHE || mode == GI_DEBUG_RADIOSITY_FRAMES)
		{
			bool has_cards = GiObjectGridHasCards(hit.position + hit.normal * 0.5 * hit.voxel);
			value = has_cards ? vec3(1.0, 0.0, 1.0) : vec3(1.0, 1.0, 0.0);
		}
	}
	vec4 color = GiDebugFinalize(value, mode);
	BRANCH
	if(u_gi_debug_show_coverage)
	{
		color.xyz = mix(color.xyz, vec3(1.0, 0.0, 1.0), 1.0 - SdfSampleClipmapCoverage(hit.position));
	}
	return color;
}

/// The furthest stage of GiSampleCard any card of @p instance reaches: 0 = none faces the normal,
/// 1 = facing but the box misses, 2 = in a box but the page is unmapped, 3 = depth test fails, 4 = covered.
int GiCoverageStage(int instance, vec3 position, vec3 normal, float base_bias)
{
	vec4 cards = GiLoadInstanceCards(instance);
	int count = int(cards.y);
	float bias = base_bias + (cards.z > 0.5 ? GI_TWO_SIDED_SURFACE_CACHE_BIAS : 0.0);
	int stage = 0;
	LOOP
	for(int i = 0; i < count; ++i)
	{
		GiCard card = GiLoadCard(int(cards.x) + i);
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
		GiCardSample s = GiComputeCardSample(card, local.xy);
		if(!s.valid)
		{
			continue;
		}
		stage = max(stage, 3);
		vec4 weighted = GiSampleCard(int(cards.x) + i, position, normal, bias, s_gi_card_albedo);
		if(weighted.w > 0.0)
		{
			stage = 4;
		}
	}
	return stage;
}

vec4 GiCoverageColor(int stage)
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
vec4 GiCoverageView(vec2 uv)
{
	GiDebugHit hit = GiTraceMeshSdfView(uv);
	if(!hit.hit)
	{
		return vec4_splat(0.0);
	}
	if(hit.instance < 0 || float(hit.instance) >= u_gi_surface_cache.w)
	{
		return vec4(0.3, 0.3, 0.3, 1.0);
	}
	if(GiLoadInstanceCards(hit.instance).y < 0.5)
	{
		return vec4(0.0, 0.0, 1.0, 1.0);
	}
	return GiCoverageColor(GiCoverageStage(hit.instance, hit.position, hit.normal, GI_MESH_SDF_SURFACE_CACHE_BIAS));
}

/// The furthest card stage over the placements the object grid lists at each global distance-field hit (mode 5).
vec4 GiObjectGridView(vec2 uv, vec2 pixel)
{
	GiDebugHit hit = GiTraceGlobalSdfView(uv, pixel);
	if(!hit.hit)
	{
		return vec4_splat(0.0);
	}
	vec4 ids = GiObjectGridInstances(hit.position + hit.normal * 0.5 * hit.voxel);
	if(ids.x < 0.5)
	{
		return vec4(0.3, 0.3, 0.3, 1.0);
	}
	float bias = GI_GLOBAL_SDF_SURFACE_CACHE_BIAS * u_gi_object_grid_params.y * 0.5 * hit.voxel;
	int stage = GiCoverageStage(int(ids.x) - 1, hit.position, hit.normal, bias);
	if(ids.y > 0.5)
	{
		stage = max(stage, GiCoverageStage(int(ids.y) - 1, hit.position, hit.normal, bias));
	}
	if(ids.z > 0.5)
	{
		stage = max(stage, GiCoverageStage(int(ids.z) - 1, hit.position, hit.normal, bias));
	}
	if(ids.w > 0.5)
	{
		stage = max(stage, GiCoverageStage(int(ids.w) - 1, hit.position, hit.normal, bias));
	}
	return GiCoverageColor(stage);
}

vec4 GiAtlasView(vec2 uv)
{
	// Square atlas centred in the viewport.
	float aspect = u_gi_debug2.x;
	vec2 atlas_uv = aspect >= 1.0 ? vec2((uv.x - 0.5) * aspect + 0.5, uv.y) : vec2(uv.x, (uv.y - 0.5) / aspect + 0.5);
	if(any(lessThan(atlas_uv, vec2_splat(0.0))) || any(greaterThan(atlas_uv, vec2_splat(1.0))))
	{
		return vec4(0.0, 0.0, 0.0, 1.0);
	}
	ivec2 size = textureSize(s_gi_card_albedo, 0);
	ivec2 texel = min(ivec2(atlas_uv * vec2(size)), size - ivec2(1, 1));
	vec4 albedo = texelFetch(s_gi_card_albedo, texel, 0);
	return vec4(albedo.xyz * albedo.xyz, 1.0);
}

void main()
{
	vec2 uv = v_texcoord0;
	vec2 tile_size = u_gi_debug3.xy;
	if(tile_size.x > 0.0 && !GiIsInsideVisualizeTile(floor(uv * tile_size), tile_size))
	{
		discard;
	}
	// The view's pixel this uv falls in, also inside a tile, so a tile dithers as the whole view does.
	vec2 pixel = floor(uv * u_gi_debug3.zw) + 0.5;
	int mode = int(u_gi_debug.x + 0.5);
	// Explicit branches: fxc evaluates both sides of ?:, and the scene views trace.
	BRANCH
	if(mode == GI_DEBUG_ATLAS)
	{
		gl_FragColor = GiAtlasView(uv);
	}
	else if(mode == GI_DEBUG_COVERAGE)
	{
		gl_FragColor = GiCoverageView(uv);
	}
	else if(mode == GI_DEBUG_OBJECT_GRID)
	{
		gl_FragColor = GiObjectGridView(uv, pixel);
	}
	else
	{
		gl_FragColor = GiSceneView(uv, pixel, mode);
	}
}
