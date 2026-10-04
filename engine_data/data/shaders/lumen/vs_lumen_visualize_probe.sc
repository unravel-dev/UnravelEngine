$output v_sphere, v_world_position, v_probe

/*
 * UE 5.8's probe visualizations as spheres, drawn without vertex buffers: 36 vertices of a cube around each probe
 * slot's sphere, which the pixel shader ray-casts.
 *  mode 0, the radiosity probes (r.LumenScene.Radiosity.VisualizeProbes: Radiosity/LumenVisualizeRadiosityProbes.usf
 *          BuildVisualizeProbesCS + VisualizeRadiosityProbesVS): a resident page has a slot per cell of
 *          u_lumen_radiosity_spacing^2 of its texels (lumen_radiosity_common.sh); the probe sits at the mean world
 *          position of the cell's (up to) four middle texels the cards captured. It is valid when the jittered texel
 *          its last radiosity update traced from holds a surface; invalid probes show only on request.
 *  mode 1, the radiance cache probes (r.Lumen.RadianceCache.Visualize 1: LumenVisualizeRadianceCache.usf
 *          BuildProbeVisualizeBufferCS + VisualizeRadianceCacheVS): a slot per cell of the clipmaps drawn; a cell
 *          holding a probe draws it at its lattice point, a sphere of the radius scale x the clipmap's cell size.
 */

#include "../common.sh"
#include "../sampling.sh"
#include <bgfx_compute.sh>

BUFFER_RO(b_lumen_scene, vec4, 0);
/// Per resident page, 3 vec4 (lumen_scene::get_visualized_pages): (atlas origin xy, size xy), the card UV rectangle,
/// (card index, the last radiosity update's index, 0, 0).
BUFFER_RO(b_lumen_visualize_pages, vec4, 1);
SAMPLER2D(s_lumen_card_depth, 2);
/// The radiance cache's indirection (lumen_radiance_cache_common.sh), mode 1.
BUFFER_RO(b_lumen_rc_indirection, uint, 6);

#define LUMEN_SURFACE_CACHE_TABLES_ONLY
#include "lumen/lumen_surface_cache.sh"
#include "lumen/lumen_radiosity_common.sh"
#include "lumen/lumen_radiance_cache_common.sh"

/// x = mode (0 = the radiosity probes, 1 = the radiance cache probes), y = the sphere radius in metres (mode 0) or
/// in cell sizes (mode 1), z = 1 to show invalid probes (mode 0) or the first clipmap drawn (mode 1), w = probe slots
/// per page (mode 0).
uniform vec4 u_lumen_visualize_probe;

#define LUMEN_VISUALIZE_MODE_RADIANCE_CACHE 1

#define LUMEN_VISUALIZE_PAGE_STRIDE 3
#define LUMEN_VISUALIZE_CUBE_VERTICES 36

/// Corner @p corner (0-5) of face @p face (0-5: -x, +x, -y, +y, -z, +z) of the cube [-1, 1]^3, two triangles a face.
vec3 LumenCubeVertex(int face, int corner)
{
	int axis = face / 2;
	float side = (face - axis * 2) == 0 ? -1.0 : 1.0;
	int quad = corner < 3 ? corner : corner - 2;
	vec2 uv = vec2((quad == 1 || quad == 3) ? 1.0 : -1.0, quad >= 2 ? 1.0 : -1.0);
	if(axis == 0)
	{
		return vec3(side, uv.x, uv.y);
	}
	if(axis == 1)
	{
		return vec3(uv.x, side, uv.y);
	}
	return vec3(uv.x, uv.y, side);
}

void main()
{
	int vertex_index = int(gl_VertexID);
	int slot = vertex_index / LUMEN_VISUALIZE_CUBE_VERTICES;
	int local = vertex_index - slot * LUMEN_VISUALIZE_CUBE_VERTICES;
	int slots_per_page = int(u_lumen_visualize_probe.w);
	int page_index = slot / slots_per_page;
	int probe = slot - page_index * slots_per_page;
	vec4 page = b_lumen_visualize_pages[page_index * LUMEN_VISUALIZE_PAGE_STRIDE + 0];
	vec4 uv_rect = b_lumen_visualize_pages[page_index * LUMEN_VISUALIZE_PAGE_STRIDE + 1];
	vec4 owner = b_lumen_visualize_pages[page_index * LUMEN_VISUALIZE_PAGE_STRIDE + 2];
	int spacing = u_lumen_radiosity_spacing;
	int cells_per_row = int(LUMEN_PHYSICAL_PAGE_SIZE) / spacing;
	ivec2 cell = ivec2(probe - (probe / cells_per_row) * cells_per_row, probe / cells_per_row);
	ivec2 cells = ivec2(page.zw) / spacing;
	// A slot without a probe collapses (UE: OutPosition = 0). No early return: bgfx's vertex main returns its
	// varyings.
	vec4 position = vec4_splat(0.0);
	vec4 sphere = vec4_splat(0.0);
	vec3 world = vec3_splat(0.0);
	vec4 probe_data = vec4_splat(0.0);
	int face = local / 6;
	vec3 cube = LumenCubeVertex(face, local - face * 6);
	BRANCH
	if(int(u_lumen_visualize_probe.x + 0.5) == LUMEN_VISUALIZE_MODE_RADIANCE_CACHE)
	{
		int grid = LUMEN_RADIANCE_CACHE_GRID;
		int cells_per_clipmap = grid * grid * grid;
		int clipmap = int(u_lumen_visualize_probe.z) + slot / cells_per_clipmap;
		int cell_index = slot - (slot / cells_per_clipmap) * cells_per_clipmap;
		ivec3 rc_cell = ivec3(cell_index - (cell_index / grid) * grid,
		                      (cell_index / grid) - (cell_index / (grid * grid)) * grid,
		                      cell_index / (grid * grid));
		uint probe_index = b_lumen_rc_indirection[LumenRcIndirectionIndex(rc_cell, clipmap)];
		if(clipmap < LUMEN_RADIANCE_CACHE_CLIPMAPS && probe_index != LUMEN_RC_INVALID && probe_index != LUMEN_RC_USED)
		{
			vec3 center = LumenRcProbePosition(rc_cell, clipmap);
			float radius = u_lumen_visualize_probe.y * LumenRcCellSize(clipmap);
			world = center + cube * radius;
			position = mul(u_viewProj, vec4(world, 1.0));
			sphere = vec4(center, radius);
			probe_data = vec4(float(probe_index), 1.0, 0.0, 0.0);
		}
	}
	else if(cell.x < cells.x && cell.y < cells.y)
	{
		LumenCard card = LumenLoadCard(int(owner.x));
		ivec2 cell_origin = ivec2(page.xy) + cell * spacing;
		ivec2 lower = cell_origin + (spacing - 1) / 2;
		vec3 position_sum = vec3_splat(0.0);
		float valid_texels = 0.0;
		for(int i = 0; i < 4; ++i)
		{
			ivec2 texel = lower + ivec2(i - (i / 2) * 2, i / 2);
			float depth = texelFetch(s_lumen_card_depth, texel, 0).x;
			if(depth < 1.0)
			{
				position_sum += LumenTexelPosition(card, uv_rect, page, texel, depth);
				valid_texels += 1.0;
			}
		}
		ivec2 probe_texel = cell_origin + LumenRadiosityJitter(owner.y);
		bool is_valid = texelFetch(s_lumen_card_depth, probe_texel, 0).x < 1.0;
		if(valid_texels > 0.5 && (is_valid || u_lumen_visualize_probe.z > 0.5))
		{
			vec3 center = position_sum / valid_texels;
			float radius = u_lumen_visualize_probe.y;
			world = center + cube * radius;
			position = mul(u_viewProj, vec4(world, 1.0));
			sphere = vec4(center, radius);
			probe_data = vec4(vec2(cell_origin / spacing), is_valid ? 1.0 : 0.0, 0.0);
		}
	}
	gl_Position = position;
	v_sphere = sphere;
	v_world_position = world;
	v_probe = probe_data;
}
