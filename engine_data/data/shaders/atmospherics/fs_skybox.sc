$input v_direction

#include "../common.sh"

SAMPLERCUBE(s_texCube, 0);

void main()
{
    vec3 dir = normalize(v_direction);
    vec4 color = textureCube(s_texCube, dir);
    gl_FragColor = color;
}
