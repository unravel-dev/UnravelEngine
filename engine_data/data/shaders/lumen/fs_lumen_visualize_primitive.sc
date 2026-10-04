$input v_color0

/*
 * The Lumen visualizations' world-space primitives: the linear colour as radiance (UE's emissive debug material), in
 * the pre-exposed scene colour, with the colour's alpha for the translucent ones.
 */

#include "../common.sh"

/// x = the view's pre-exposure.
uniform vec4 u_lumen_visualize_primitive;

void main()
{
	gl_FragColor = vec4(v_color0.rgb * u_lumen_visualize_primitive.x, v_color0.a);
}
