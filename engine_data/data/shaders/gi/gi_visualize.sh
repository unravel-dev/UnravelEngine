#ifndef __GI_VISUALIZE_SH__
#define __GI_VISUALIZE_SH__

/*
 * Shared by the GI debug views: the overview tiles of fs_gi_scene_debug.sc and fs_gi_visualize_screen.sc, and
 * the layout of the visualizations' lines (cs_gi_probe_trace_visualize.sc and
 * cs_gi_visualize_reflection_trace.sc write them, vs_gi_visualize_lines.sc draws them).
 */

/// The rounding of a tile's corners, in pixels.
#define GI_VISUALIZE_TILE_BORDER 12.0
/// vec4s per line: the start's world position, the vector to the end (a ray's direction times its hit distance), the
/// colour (a ray's pre-exposed radiance).
#define GI_VISUALIZE_TRACE_STRIDE 3

/// True when @p pixel (0 .. @p size - 1) lies inside a tile of @p size pixels with corners rounded over
/// GI_VISUALIZE_TILE_BORDER pixels.
bool GiIsInsideVisualizeTile(vec2 pixel, vec2 size)
{
	vec2 rect_min = vec2_splat(GI_VISUALIZE_TILE_BORDER);
	vec2 rect_max = size - GI_VISUALIZE_TILE_BORDER - 1.0;
	vec2 outside = max(max(rect_min - pixel, pixel - rect_max), vec2_splat(0.0));
	return length(outside) - GI_VISUALIZE_TILE_BORDER < 0.5;
}

#endif // __GI_VISUALIZE_SH__
