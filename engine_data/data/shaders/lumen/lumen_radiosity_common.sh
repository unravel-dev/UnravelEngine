#ifndef __LUMEN_RADIOSITY_COMMON_SH__
#define __LUMEN_RADIOSITY_COMMON_SH__

/*
 * Surface cache radiosity, shared layout: probes every u_lumen_radiosity_spacing card texels (4; 2 from the
 * surface cache lighting quality 6), each at its cell's texel
 * offset by a jitter that cycles over four updates of the page, tracing u_lumen_radiosity_resolution^2 stratified,
 * noise-jittered uniform hemisphere rays (2 x 2 to 8 x 8 with the lighting quality, 4 x 4 at 1;
 * lumen_pass::get_radiosity_layout). A probe's traces fill an R x R tile of the trace atlas at its cell coordinate x R
 * (the trace atlas spans (atlas / spacing) x R texels per axis).
 *
 * Work is scheduled in card tiles of LUMEN_RADIOSITY_TILE_TEXELS^2 texels ((8 / spacing)^2 probes); a tile record is
 * 3 vec4: (tile origin in the atlas xy, card index, the page's update index), the page's card UV rectangle, (page
 * atlas origin xy, page size xy). The trace and the filter run groups of LUMEN_RADIOSITY_GROUP_THREADS over a tile's
 * traces, probe-major.
 *
 * A card larger than one physical page spreads its probes over pages anywhere in the atlas. The filter's neighbours
 * and the integrate's bilinear probes past a page edge are read in the card's neighbouring page, through its page
 * table, with that page's jitter (LumenResolveRadiosityCell): the lighting does not step at page edges.
 */

/// x = the ray clamp in cached units (max ray intensity / the view's pre-exposure; the trace), y = the probe spacing in
/// card texels, z = the rays per axis of a probe's hemisphere, w = the float4 of b_lumen_light_tiles the per-page
/// update indices start at (lumen_scene::get_page_radiosity_indices), negative when the probes stop at page edges.
uniform vec4 u_lumen_radiosity;

#define u_lumen_radiosity_max_ray_intensity u_lumen_radiosity.x
#define u_lumen_radiosity_spacing           int(u_lumen_radiosity.y)
#define u_lumen_radiosity_resolution        int(u_lumen_radiosity.z)
#define u_lumen_radiosity_page_words        u_lumen_radiosity.w

/// The card tile's edge in texels (the lighting kernels' 8 x 8 tiles) and the threads of a trace or filter group.
#define LUMEN_RADIOSITY_TILE_TEXELS 8
#define LUMEN_RADIOSITY_GROUP_THREADS 64
/// The most frames the radiosity history accumulates; the probe jitter cycles over as many updates.
#define LUMEN_RADIOSITY_MAX_FRAMES 4.0
/// Ray start offsets: a 5 cm surface bias along the normal and the ray, a 10 cm minimum trace distance.
#define LUMEN_RADIOSITY_SURFACE_BIAS 0.05
#define LUMEN_RADIOSITY_MIN_TRACE_DISTANCE 0.1
/// The radiosity rays' maximum trace distance (200 m).
#define LUMEN_RADIOSITY_MAX_TRACE_DISTANCE 200.0
/// The clamp on a ray's radiance (max ray intensity), in pre-exposed units.
#define LUMEN_RADIOSITY_MAX_RAY_INTENSITY 40.0
/// The plane test between a probe and a texel or neighbour: exp2(-100 rel^2) > 0.01.
#define LUMEN_RADIOSITY_PLANE_REJECT 0.01
#define LUMEN_RADIOSITY_PLANE_SCALE 100.0
#define LUMEN_RADIOSITY_PLANE_MIN_REL 0.1
#define LUMEN_TWO_PI 6.28318531

/// The probe jitter of update @p index: Hammersley16(i % 4, 4, (0x4ae4, 0x9bdb)) x the spacing,
/// truncated - (1, 2), (2, 0), (3, 3), (0, 1) at a spacing of 4.
ivec2 LumenRadiosityJitter(float index)
{
	uint frame = uint(mod(index, LUMEN_RADIOSITY_MAX_FRAMES));
	vec2 hammersley = Hammersley16(frame, uint(LUMEN_RADIOSITY_MAX_FRAMES), uvec2(0x4ae4u, 0x9bdbu));
	return ivec2(hammersley * float(u_lumen_radiosity_spacing));
}

#ifdef LUMEN_TILE_RECORDS_LIGHT_TILES
/// A probe cell of a card, resolved to the physical page holding it (the card passes: lumen_tile_records.sh's light
/// tiles and the card tables of lumen_surface_cache.sh).
struct LumenRadiosityCell
{
	/// False outside the card, on an unmapped page, or on a page the radiosity has not updated since it was mapped.
	bool valid;
	/// The cell's first texel in the atlas (its probe sits there plus its page's jitter).
	ivec2 atlas_origin;
	/// The cell's page: atlas origin xy, size xy.
	vec4 page;
	/// The card UV rectangle of the cell's page.
	vec4 uv_rect;
	/// The update index of the page's last radiosity update (its probes' jitter and ray directions).
	float update_index;
};

/// The (x, y) of a page in its card's grid of @p size_in_pages, from the card UV rectangle @p uv_rect it covers.
ivec2 LumenRadiosityPageCoord(vec4 uv_rect, vec2 size_in_pages)
{
	return ivec2(floor(0.5 * (uv_rect.xy + uv_rect.zw) * size_in_pages));
}

/// The card UV rectangle of the page at @p coord of a card of @p size_in_pages pages (lumen_scene compute_page_uv_rect):
/// half a texel of border on interior page edges.
vec4 LumenRadiosityPageUvRect(ivec2 coord, vec2 size_in_pages)
{
	vec2 low = vec2(coord) / size_in_pages;
	vec2 high = vec2(coord + ivec2(1, 1)) / size_in_pages;
	vec2 border = 0.5 / (size_in_pages * LUMEN_PHYSICAL_PAGE_SIZE);
	low -= vec2(coord.x > 0 ? border.x : 0.0, coord.y > 0 ? border.y : 0.0);
	high += vec2(float(coord.x + 1) < size_in_pages.x ? border.x : 0.0, float(coord.y + 1) < size_in_pages.y ? border.y : 0.0);
	return vec4(low, high);
}

/// Probe cell @p cell_in_page (in cells from the page's origin; negative or past the page's edge for its neighbours) of
/// the page at @p page (atlas origin xy, size xy) covering @p uv_rect of @p card, whose last update was @p update_index.
LumenRadiosityCell LumenResolveRadiosityCell(LumenCard card, vec4 uv_rect, vec4 page, float update_index, ivec2 cell_in_page)
{
	LumenRadiosityCell cell;
	ivec2 page_size = ivec2(page.zw);
	ivec2 cell_texel = cell_in_page * u_lumen_radiosity_spacing;
	cell.valid = all(greaterThanEqual(cell_texel, ivec2(0, 0))) && all(lessThan(cell_texel, page_size));
	cell.atlas_origin = ivec2(page.xy) + cell_texel;
	cell.page = page;
	cell.uv_rect = uv_rect;
	cell.update_index = update_index;
	BRANCH
	if(cell.valid || u_lumen_radiosity_page_words < 0.0)
	{
		return cell;
	}
	ivec2 card_texel = LumenRadiosityPageCoord(uv_rect, card.size_in_pages) * page_size + cell_texel;
	ivec2 card_size = ivec2(card.size_in_pages) * page_size;
	if(any(lessThan(card_texel, ivec2(0, 0))) || any(greaterThanEqual(card_texel, card_size)))
	{
		return cell;
	}
	ivec2 other = card_texel / page_size;
	int entry = int(card.page_table_offset) + other.x + other.y * int(card.size_in_pages.x);
	vec4 mapping = b_lumen_scene[int(u_lumen_surface_cache.y) + entry];
	float other_index =
	    LumenTileWord(b_lumen_light_tiles[int(u_lumen_radiosity_page_words) + entry / LUMEN_TILE_WORDS_PER_VEC4], entry);
	cell.valid = mapping.z > 0.0 && other_index >= 0.0;
	cell.atlas_origin = ivec2(mapping.xy) + card_texel - other * page_size;
	cell.page = vec4(mapping.xy, page.zw);
	cell.uv_rect = LumenRadiosityPageUvRect(other, card.size_in_pages);
	cell.update_index = other_index;
	return cell;
}
#endif

/// The probes along a card tile's axis.
int LumenRadiosityProbesPerTileAxis()
{
	return LUMEN_RADIOSITY_TILE_TEXELS / u_lumen_radiosity_spacing;
}

/// The (x, y) of @p index in a row-major grid @p width wide.
ivec2 LumenRadiosityGridCoord(int index, int width)
{
	int row = index / width;
	return ivec2(index - row * width, row);
}

/// The trace atlas texel of trace @p trace_texel of the probe at card cell @p probe_cell.
ivec2 LumenRadiosityTraceTexel(ivec2 probe_cell, ivec2 trace_texel)
{
	return probe_cell * u_lumen_radiosity_resolution + trace_texel;
}

/// A uniform hemisphere sample around +z: z = cos theta uniform in [0, 1).
vec3 LumenUniformSampleHemisphere(vec2 e)
{
	float phi = LUMEN_TWO_PI * e.x;
	float cos_theta = e.y;
	float sin_theta = sqrt(max(1.0 - cos_theta * cos_theta, 0.0));
	return vec3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);
}

/// Frisvad's basis around @p n applied to a +z-hemisphere vector.
vec3 LumenTangentToWorld(vec3 n, vec3 v)
{
	vec3 tangent_x = vec3(0.0, -1.0, 0.0);
	vec3 tangent_y = vec3(-1.0, 0.0, 0.0);
	if(n.z >= -0.9999999)
	{
		float a = 1.0 / (1.0 + n.z);
		float b = -n.x * n.y * a;
		tangent_x = vec3(1.0 - n.x * n.x * a, b, -n.x);
		tangent_y = vec3(b, 1.0 - n.y * n.y * a, -n.y);
	}
	return tangent_x * v.x + tangent_y * v.y + n * v.z;
}

/// The direction of trace @p trace_texel of the probe at atlas cell @p probe_cell, update @p index.
vec3 LumenRadiosityRayDirection(vec3 normal, ivec2 probe_cell, ivec2 trace_texel, float index)
{
	vec2 noise = SpatioTemporalNoise2D(vec2(probe_cell), index);
	vec2 e = (vec2(trace_texel) + noise) / float(u_lumen_radiosity_resolution);
	return LumenTangentToWorld(normal, LumenUniformSampleHemisphere(e));
}

/// True when @p other lies within ~15 degrees of the tangent plane at @p position.
bool LumenRadiosityPlaneTest(vec3 position, vec3 normal, vec3 other)
{
	vec3 delta = other - position;
	float rel = max(abs(dot(normal, delta)) / (length(delta) + 0.0001), LUMEN_RADIOSITY_PLANE_MIN_REL);
	return exp2(-LUMEN_RADIOSITY_PLANE_SCALE * rel * rel) > LUMEN_RADIOSITY_PLANE_REJECT;
}

/// The real SH basis of two bands at @p d (order 1, y, z, x).
vec4 LumenSH2Basis(vec3 d)
{
	return vec4(0.282095, -0.488603 * d.y, 0.488603 * d.z, -0.488603 * d.x);
}

/// The cosine lobe around @p n in two SH bands: the basis scaled by the lobe's band factors (pi, 2pi/3).
vec4 LumenSH2DiffuseTransfer(vec3 n)
{
	return LumenSH2Basis(n) * vec4(3.14159265, 2.09439510, 2.09439510, 2.09439510);
}

#endif // __LUMEN_RADIOSITY_COMMON_SH__
