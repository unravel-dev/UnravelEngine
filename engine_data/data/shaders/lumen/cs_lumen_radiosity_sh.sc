/*
 * Lumen surface cache radiosity, spatial filter and SH (UE 5.8 LumenRadiosity.usf:175-385): per scheduled card tile,
 * groups of LUMEN_RADIOSITY_GROUP_THREADS holding the rays of as many whole probes as fit (lumen_radiosity_common.sh),
 * one thread per ray, the group's y the block of the tile's probes.
 *
 * Filter: each trace is averaged with the same trace of the four neighbouring probes of the page (centre
 * weight 2), a neighbour counting only when its probe is a valid texel within ~15 degrees of this probe's
 * tangent plane. SH: each probe projects its filtered traces onto two SH bands (the uniform hemisphere pdf
 * folded in) and stores them per colour channel at its probe cell.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "../sampling.sh"

IMAGE2D_WO(s_lumen_radiosity_sh_r_out, rgba16f, 0);
IMAGE2D_WO(s_lumen_radiosity_sh_g_out, rgba16f, 1);
IMAGE2D_WO(s_lumen_radiosity_sh_b_out, rgba16f, 2);
SAMPLER2D(s_lumen_radiosity_trace, 3);
SAMPLER2D(s_lumen_card_depth, 4);
SAMPLER2D(s_lumen_card_normal, 5);
BUFFER_RO(b_lumen_light_tiles, vec4, 6);
BUFFER_RO(b_lumen_scene, vec4, 7);

#define LUMEN_SURFACE_CACHE_TABLES_ONLY
#include "lumen/lumen_surface_cache.sh"
#include "lumen/lumen_radiosity_common.sh"

/// x = first tile of this dispatch, y = tile count.
uniform vec4 u_lumen_card_lighting;

/// Filtered radiance per thread (probe-major: the group's probes x their traces).
SHARED vec3 s_filtered[LUMEN_RADIOSITY_GROUP_THREADS];

struct LumenRadiosityProbe
{
	bool valid;
	vec3 position;
	vec3 normal;
};

LumenRadiosityProbe LumenLoadRadiosityProbe(LumenCard card, vec4 uv_rect, vec4 page, ivec2 probe_texel)
{
	LumenRadiosityProbe probe;
	float depth = texelFetch(s_lumen_card_depth, probe_texel, 0).x;
	probe.valid = depth < 1.0;
	vec2 card_uv = mix(uv_rect.xy, uv_rect.zw, (vec2(probe_texel) - page.xy + 0.5) / page.zw);
	probe.position = LumenCardWorldPosition(card, LumenCardLocalPosition(card, card_uv, min(depth, 1.0)));
	probe.normal = decodeNormalOctahedron(texelFetch(s_lumen_card_normal, probe_texel, 0).xy);
	return probe;
}

/// Adds the same trace of the neighbouring probe at cell offset @p offset (in cells) when it qualifies.
vec4 LumenAddNeighbour(vec4 accumulated, LumenCard card, vec4 uv_rect, vec4 page, ivec2 cell_origin, ivec2 offset,
                       ivec2 jitter, ivec2 trace_texel, LumenRadiosityProbe self_probe)
{
	int spacing = u_lumen_radiosity_spacing;
	ivec2 neighbour_origin = cell_origin + offset * spacing;
	ivec2 page_min = ivec2(page.xy);
	ivec2 page_max = page_min + ivec2(page.zw);
	if(any(lessThan(neighbour_origin, page_min)) || any(greaterThanEqual(neighbour_origin, page_max)))
	{
		return accumulated;
	}
	LumenRadiosityProbe neighbour = LumenLoadRadiosityProbe(card, uv_rect, page, neighbour_origin + jitter);
	if(!neighbour.valid || !LumenRadiosityPlaneTest(self_probe.position, self_probe.normal, neighbour.position))
	{
		return accumulated;
	}
	ivec2 trace = LumenRadiosityTraceTexel(neighbour_origin / spacing, trace_texel);
	vec3 radiance = texelFetch(s_lumen_radiosity_trace, trace, 0).xyz;
	return accumulated + vec4(radiance, 1.0);
}

NUM_THREADS(LUMEN_RADIOSITY_GROUP_THREADS, 1, 1)
void main()
{
	int tile_index = int(u_lumen_card_lighting.x) + int(gl_WorkGroupID.x);
	bool is_tile_active = float(gl_WorkGroupID.x) < u_lumen_card_lighting.y;
	vec4 t0 = b_lumen_light_tiles[tile_index * LUMEN_RADIOSITY_TILE_STRIDE + 0];
	vec4 uv_rect = b_lumen_light_tiles[tile_index * LUMEN_RADIOSITY_TILE_STRIDE + 1];
	vec4 page = b_lumen_light_tiles[tile_index * LUMEN_RADIOSITY_TILE_STRIDE + 2];
	int spacing = u_lumen_radiosity_spacing;
	int resolution = u_lumen_radiosity_resolution;
	int probes_per_axis = LumenRadiosityProbesPerTileAxis();
	int rays_per_probe = resolution * resolution;
	int probes_per_group = LUMEN_RADIOSITY_GROUP_THREADS / rays_per_probe;
	int local = int(gl_LocalInvocationIndex);
	int probe_slot = local / rays_per_probe;
	int trace_slot = local - probe_slot * rays_per_probe;
	int probe = int(gl_WorkGroupID.y) * probes_per_group + probe_slot;
	// Barriers stay in uniform flow control: the threads past the group's probes run the same steps.
	bool is_probe_active =
	    is_tile_active && probe_slot < probes_per_group && probe < probes_per_axis * probes_per_axis;
	ivec2 trace_texel = LumenRadiosityGridCoord(trace_slot, resolution);
	ivec2 cell_origin = ivec2(t0.xy) + LumenRadiosityGridCoord(probe, probes_per_axis) * spacing;
	ivec2 jitter = LumenRadiosityJitter(t0.w);
	LumenCard card = LumenLoadCard(int(t0.z));
	LumenRadiosityProbe self_probe = LumenLoadRadiosityProbe(card, uv_rect, page, cell_origin + jitter);
	vec3 filtered = vec3_splat(0.0);
	if(is_probe_active && self_probe.valid)
	{
		ivec2 trace = LumenRadiosityTraceTexel(cell_origin / spacing, trace_texel);
		vec4 accumulated = vec4(2.0 * texelFetch(s_lumen_radiosity_trace, trace, 0).xyz, 2.0);
		accumulated = LumenAddNeighbour(accumulated, card, uv_rect, page, cell_origin, ivec2(0, 1), jitter, trace_texel, self_probe);
		accumulated = LumenAddNeighbour(accumulated, card, uv_rect, page, cell_origin, ivec2(1, 0), jitter, trace_texel, self_probe);
		accumulated = LumenAddNeighbour(accumulated, card, uv_rect, page, cell_origin, ivec2(0, -1), jitter, trace_texel, self_probe);
		accumulated = LumenAddNeighbour(accumulated, card, uv_rect, page, cell_origin, ivec2(-1, 0), jitter, trace_texel, self_probe);
		filtered = accumulated.xyz / accumulated.w;
	}
	s_filtered[local] = filtered;
	barrier();
	// One thread per probe projects its traces.
	if(!is_probe_active || trace_slot != 0)
	{
		return;
	}
	vec4 sh_r = vec4_splat(0.0);
	vec4 sh_g = vec4_splat(0.0);
	vec4 sh_b = vec4_splat(0.0);
	if(self_probe.valid)
	{
		// (1 / ray count) x sum of Y(dir) L / pdf, with the uniform hemisphere pdf 1 / (2 pi).
		float weight = LUMEN_TWO_PI / float(rays_per_probe);
		for(int t = 0; t < rays_per_probe; ++t)
		{
			ivec2 texel = LumenRadiosityGridCoord(t, resolution);
			vec3 direction = LumenRadiosityRayDirection(self_probe.normal, cell_origin / spacing, texel, t0.w);
			vec4 basis = LumenSH2Basis(direction) * weight;
			vec3 radiance = s_filtered[probe_slot * rays_per_probe + t];
			sh_r += basis * radiance.x;
			sh_g += basis * radiance.y;
			sh_b += basis * radiance.z;
		}
	}
	ivec2 probe_cell = cell_origin / spacing;
	imageStore(s_lumen_radiosity_sh_r_out, probe_cell, sh_r);
	imageStore(s_lumen_radiosity_sh_g_out, probe_cell, sh_g);
	imageStore(s_lumen_radiosity_sh_b_out, probe_cell, sh_b);
}
