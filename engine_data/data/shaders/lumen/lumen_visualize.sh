#ifndef __LUMEN_VISUALIZE_SH__
#define __LUMEN_VISUALIZE_SH__

/*
 * Shared by the GI debug views: the overview tiles of fs_lumen_scene_debug.sc and fs_lumen_visualize_screen.sc, and
 * the layout of the visualizations' lines (cs_lumen_probe_trace_visualize.sc and
 * cs_lumen_visualize_reflection_trace.sc write them, vs_lumen_visualize_lines.sc draws them).
 */

/// The rounding of a tile's corners, in pixels.
#define LUMEN_VISUALIZE_TILE_BORDER 12.0
/// vec4s per line: the start's world position, the vector to the end (a ray's direction times its hit distance), the
/// colour (a ray's pre-exposed radiance).
#define LUMEN_VISUALIZE_TRACE_STRIDE 3

/// True when @p pixel (0 .. @p size - 1) lies inside a tile of @p size pixels with corners rounded over
/// LUMEN_VISUALIZE_TILE_BORDER pixels.
bool LumenIsInsideVisualizeTile(vec2 pixel, vec2 size)
{
	vec2 rect_min = vec2_splat(LUMEN_VISUALIZE_TILE_BORDER);
	vec2 rect_max = size - LUMEN_VISUALIZE_TILE_BORDER - 1.0;
	vec2 outside = max(max(rect_min - pixel, pixel - rect_max), vec2_splat(0.0));
	return length(outside) - LUMEN_VISUALIZE_TILE_BORDER < 0.5;
}

#endif // __LUMEN_VISUALIZE_SH__
