$input v_direction

#include "../common.sh"

SAMPLERCUBE(s_texCube, 0);
uniform vec4 u_sky_brightness;

void main()
{
    vec3 dir = normalize(v_direction);
    vec4 color = textureCube(s_texCube, dir);
    color.rgb *= u_sky_brightness.x;
    gl_FragColor = color;
}
