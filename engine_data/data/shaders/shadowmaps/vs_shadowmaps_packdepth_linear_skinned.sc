$input a_position, a_weight, a_indices
$output v_depth

/*
 * Copyright 2013-2014 Dario Manesku. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#include "../common.sh"

void main()
{
    //u_world should already be in the right space
    mat4 model = a_weight.x * u_world[int(a_indices.x)] +
                 a_weight.y * u_world[int(a_indices.y)] +
                 a_weight.z * u_world[int(a_indices.z)] +
                 a_weight.w * u_world[int(a_indices.w)];
					
	vec4 wpos = mul(model, vec4(a_position, 1.0) );
	gl_Position = mul(u_viewProj, wpos );
	v_depth = gl_Position.z * 0.5 + 0.5;
}
