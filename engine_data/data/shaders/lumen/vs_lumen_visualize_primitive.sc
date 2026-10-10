$input a_position, a_color0
$output v_color0

/*
 * The GI visualizations' primitives (the lines and meshes of the card placement and the card generation views):
 * positions in the space of the draw's transform (world without one) and linear colours, drawn into the scene
 * colour with its depth.
 */

#include "../common.sh"

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_color0 = a_color0;
}
