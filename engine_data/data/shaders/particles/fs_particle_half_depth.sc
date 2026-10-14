$input v_texcoord0

/*
 * The half-resolution particles' depth: the farthest of the scene depth under each texel. A particle
 * then shows on every low-resolution texel the background reaches, and the composite's nearest-depth
 * upsample keeps it off the foreground texels.
 */

#include "../common.sh"

// The view's full-resolution device depth.
SAMPLER2D(s_depth, 0);

/// Full-resolution texels a half-resolution texel covers along an axis: two, or three where an odd view size
/// makes the ratio between the two resolutions fall short of two.
#define PARTICLE_HALF_FOOTPRINT 3

void main()
{
	ivec2 size = textureSize(s_depth, 0);
	ivec2 last = size - ivec2(1, 1);
	// The span of full-resolution texels under this texel, as the composite's upsample maps one onto the other.
	vec2 ratio = vec2(size) / max(u_viewRect.zw, vec2_splat(1.0));
	ivec2 first = ivec2(floor((gl_FragCoord.xy - 0.5) * ratio));
	ivec2 end = ivec2(ceil((gl_FragCoord.xy + 0.5) * ratio));
	float depth = 0.0;
	for(int y = 0; y < PARTICLE_HALF_FOOTPRINT; ++y)
	{
		for(int x = 0; x < PARTICLE_HALF_FOOTPRINT; ++x)
		{
			ivec2 texel = first + ivec2(x, y);
			if(texel.x < end.x && texel.y < end.y)
			{
				depth = max(depth, texelFetch(s_depth, min(texel, last), 0).x);
			}
		}
	}
	gl_FragDepth = depth;
	gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
