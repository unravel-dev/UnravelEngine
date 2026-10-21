$input v_texcoord0

/*
 * The scene's depth at the output's size, for what draws over a resampled image with the scene's depth: each
 * output pixel takes the depth of the scene texel under its center, so an overlay hides where the image shows
 * a surface in front of it.
 */

#include "common.sh"

// The scene's device depth at the render size.
SAMPLER2D(s_depth, 0);

void main()
{
	ivec2 size = textureSize(s_depth, 0);
	ivec2 texel = clamp(ivec2(v_texcoord0 * vec2(size)), ivec2(0, 0), size - ivec2(1, 1));
	gl_FragDepth = texelFetch(s_depth, texel, 0).x;
	gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
}
