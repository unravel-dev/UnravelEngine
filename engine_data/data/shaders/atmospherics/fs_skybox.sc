$input v_direction

#include "../common.sh"
#include "../pre_exposure.sh"

SAMPLERCUBE(s_texCube, 0);
uniform vec4 u_sky_brightness;

void main()
{
    vec3 dir = normalize(v_direction);
    vec4 color = textureCube(s_texCube, dir);
    // u_sky_brightness carries the CPU's share of the pre-exposure; the GPU's share joins it here.
    color.rgb *= u_sky_brightness.x * u_pre_exposure_gpu_scale;
    gl_FragColor = color;
}
