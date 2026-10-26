$input v_texcoord0

/*
 * Copies a light's static copy back into its atlas tile before the casters that move draw over it: the depth its
 * casters that hold still left, and the float depth the atlas colour stores for it. The static copy is laid out like
 * the atlas, so the fragment's own position addresses its texel.
 */

#include "../common.sh"

// The static copy's device depth.
SAMPLER2D(s_staticDepth, 0);

// x, y: the atlas colour from the device depth, colour = depth * x + y (the colour stores ndc z * 0.5 + 0.5).
uniform vec4 u_restoreParams;

void main()
{
	float depth = texelFetch(s_staticDepth, ivec2(gl_FragCoord.xy), 0).x;
	gl_FragColor = vec4(depth * u_restoreParams.x + u_restoreParams.y, 0.0, 0.0, 0.0);
	gl_FragDepth = depth;
}
