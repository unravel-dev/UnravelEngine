$input a_position
$output v_direction

#include "../common.sh"

void main()
{
    vec4 pos = mul(u_viewProj, vec4(a_position, 1.0));
    pos.z = pos.w;  // push it to the far plane
    gl_Position = pos;
    v_direction  = a_position; // passing the local cube direction to FS
}
