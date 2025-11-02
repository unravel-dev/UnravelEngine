$input a_position, i_data0, i_data1, i_data2, i_data3
$output v_position

/*
 * Copyright 2013-2014 Dario Manesku. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 * 
 * Instanced version for static mesh batching
 */

#include "../common.sh"

void main()
{
    // Reconstruct world matrix from instance data
    // i_data3.w contains LOD parameter, i_data3.xyz is the 4th row translation
    vec4 matrix_row3 = vec4(i_data3.xyz, 1.0);
    mat4 world_matrix = mtxFromCols(i_data0, i_data1, i_data2, matrix_row3);
    
    vec4 wpos = mul(world_matrix, vec4(a_position, 1.0));
    gl_Position = mul(u_viewProj, wpos);
    v_position = gl_Position;
}
