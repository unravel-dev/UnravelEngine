$input v_color0

/*
 * The Lumen visualizations' line pixels as ShaderPrint draws its lines (ShaderPrintDrawPrimitive.usf
 * ShaderDrawDebugPS, r.ShaderPrint.DrawOccludedLines 1): in full in front of the scene depth; behind it at
 * LUMEN_VISUALIZE_OCCLUDED_SCALE on every other pixel of a checkerboard. Premultiplied over the finished image.
 */

#include "../common.sh"

/// The scene's device depth.
SAMPLER2D(s_scene_depth, 1);

/// ShaderPrint's colour scale of an occluded line pixel.
#define LUMEN_VISUALIZE_OCCLUDED_SCALE 0.4

void main()
{
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	ivec2 size = textureSize(s_scene_depth, 0);
	ivec2 depth_texel = min(ivec2(gl_FragCoord.xy * u_viewTexel.xy * vec2(size)), size - ivec2(1, 1));
	float scene_depth = texelFetch(s_scene_depth, depth_texel, 0).x;
	vec4 color = v_color0;
	if(gl_FragCoord.z > scene_depth)
	{
		int parity = pixel.x + pixel.y;
		bool is_checker_pixel = parity - (parity / 2) * 2 == 1;
		color = is_checker_pixel ? vec4(v_color0.rgb * LUMEN_VISUALIZE_OCCLUDED_SCALE, 1.0) : vec4_splat(0.0);
	}
	gl_FragColor = color;
}
