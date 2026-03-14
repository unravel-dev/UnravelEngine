$input v_texcoord0

#define POINT_LIGHT 1
#define SM_OMNI 1
#define SM_HARD 1

#include "fs_pbr_lighting.sh"

void main()
{
    vec4 shadowed, unshadowed;
    pbr_light(v_texcoord0, gl_FragCoord.xy, shadowed, unshadowed);
    gl_FragData[0] = shadowed;
    gl_FragData[1] = unshadowed;
}
