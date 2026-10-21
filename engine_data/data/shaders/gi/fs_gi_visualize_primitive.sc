$input v_color0

/*
 * The GI visualizations' world-space primitives: the linear colour as unlit emitted radiance, in
 * the pre-exposed scene colour, with the colour's alpha for the translucent ones.
 */

#include "../common.sh"
#include "../pre_exposure.sh"

/// x = the view's pre-exposure (the CPU's share; the GPU's share joins it below).
uniform vec4 u_gi_visualize_primitive;

void main()
{
	gl_FragColor = vec4(v_color0.rgb * (u_gi_visualize_primitive.x * u_pre_exposure_gpu_scale), v_color0.a);
}
